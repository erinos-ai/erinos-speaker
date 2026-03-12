#include "http_client.h"
#include "config.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "http";

// WAV header for PCM 16-bit mono
typedef struct __attribute__((packed)) {
    char     riff[4];       // "RIFF"
    uint32_t file_size;     // File size - 8
    char     wave[4];       // "WAVE"
    char     fmt_id[4];     // "fmt "
    uint32_t fmt_size;      // 16
    uint16_t format;        // 1 (PCM)
    uint16_t channels;      // 1
    uint32_t sample_rate;   // 16000
    uint32_t byte_rate;     // 32000
    uint16_t block_align;   // 2
    uint16_t bits_per_sample; // 16
    char     data_id[4];    // "data"
    uint32_t data_size;     // PCM data size
} wav_header_t;

static void build_wav_header(wav_header_t *h, size_t pcm_bytes)
{
    memcpy(h->riff, "RIFF", 4);
    h->file_size = pcm_bytes + sizeof(wav_header_t) - 8;
    memcpy(h->wave, "WAVE", 4);
    memcpy(h->fmt_id, "fmt ", 4);
    h->fmt_size = 16;
    h->format = 1;
    h->channels = CHANNELS;
    h->sample_rate = SAMPLE_RATE;
    h->byte_rate = SAMPLE_RATE * CHANNELS * (SAMPLE_BITS / 8);
    h->block_align = CHANNELS * (SAMPLE_BITS / 8);
    h->bits_per_sample = SAMPLE_BITS;
    memcpy(h->data_id, "data", 4);
    h->data_size = pcm_bytes;
}

// ─── Response buffer (grows dynamically in PSRAM) ───────────────────

typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
} resp_buf_t;

static esp_err_t on_data(esp_http_client_event_t *evt)
{
    resp_buf_t *buf = (resp_buf_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (buf->len + evt->data_len > buf->cap) {
            size_t new_cap = buf->cap * 2;
            if (new_cap < buf->len + evt->data_len) new_cap = buf->len + evt->data_len + 4096;
            uint8_t *new_data = heap_caps_realloc(buf->data, new_cap, MALLOC_CAP_SPIRAM);
            if (!new_data) {
                ESP_LOGE(TAG, "Out of memory for response");
                return ESP_FAIL;
            }
            buf->data = new_data;
            buf->cap = new_cap;
        }
        memcpy(buf->data + buf->len, evt->data, evt->data_len);
        buf->len += evt->data_len;
    }
    return ESP_OK;
}

esp_err_t voice_request(const int16_t *audio, size_t audio_samples,
                        uint8_t **response_buf, size_t *response_size)
{
    size_t pcm_bytes = audio_samples * sizeof(int16_t);
    wav_header_t wav_hdr;
    build_wav_header(&wav_hdr, pcm_bytes);

    // Build multipart body
    const char *boundary = "----ErinOSBoundary";
    const char *part_header =
        "------ErinOSBoundary\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n";
    const char *part_footer = "\r\n------ErinOSBoundary--\r\n";

    size_t header_len = strlen(part_header);
    size_t footer_len = strlen(part_footer);
    size_t wav_size = sizeof(wav_header_t) + pcm_bytes;
    size_t body_len = header_len + wav_size + footer_len;

    uint8_t *body = heap_caps_malloc(body_len, MALLOC_CAP_SPIRAM);
    if (!body) {
        ESP_LOGE(TAG, "Out of memory for request body");
        return ESP_ERR_NO_MEM;
    }

    size_t offset = 0;
    memcpy(body + offset, part_header, header_len); offset += header_len;
    memcpy(body + offset, &wav_hdr, sizeof(wav_header_t)); offset += sizeof(wav_header_t);
    memcpy(body + offset, audio, pcm_bytes); offset += pcm_bytes;
    memcpy(body + offset, part_footer, footer_len);

    // Response buffer (PSRAM)
    resp_buf_t resp = {
        .data = heap_caps_malloc(64 * 1024, MALLOC_CAP_SPIRAM),
        .len = 0,
        .cap = 64 * 1024,
    };

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/api/chat", ERINOS_HOST, ERINOS_PORT);

    char content_type[64];
    snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", boundary);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 60000,
        .event_handler = on_data,
        .user_data = &resp,
        .buffer_size_tx = 4096,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);

    esp_http_client_set_header(client, "Content-Type", content_type);
    esp_http_client_set_header(client, "X-User-ID", ERINOS_USER_ID);
    esp_http_client_set_post_field(client, (const char *)body, body_len);

    ESP_LOGI(TAG, "POST %s (%zu bytes)", url, body_len);
    esp_err_t err = esp_http_client_perform(client);

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    heap_caps_free(body);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        heap_caps_free(resp.data);
        return err;
    }

    if (status != 200) {
        ESP_LOGE(TAG, "Server returned %d", status);
        heap_caps_free(resp.data);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Response: %zu bytes", resp.len);
    *response_buf = resp.data;
    *response_size = resp.len;
    return ESP_OK;
}
