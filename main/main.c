#include "config.h"
#include "wifi.h"
#include "audio_input.h"
#include "audio_output.h"
#include "http_client.h"
#include "leds.h"

#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "main";

// WAV header is 44 bytes — skip it to get raw PCM
#define WAV_HEADER_SIZE 44

void app_main(void)
{
    ESP_LOGI(TAG, "ErinOS Voice Client starting...");

    // Init LEDs first for visual feedback
    leds_init();
    leds_set_state(LED_THINKING);

    // Connect to WiFi
    wifi_init();

    // Create I2S channels — TX and RX on the same peripheral (full-duplex).
    // Both codecs (ES8311 speaker, ES7210 mic) share MCLK/BCLK/WS pins.
    i2s_chan_handle_t tx_chan = NULL;
    i2s_chan_handle_t rx_chan = NULL;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 480;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_chan, &rx_chan));

    // Both TX (ES8311) and RX (ES7210) share the same I2S peripheral,
    // so both must use the same mode. ES7210 with TDM disabled (reg 0x06=0x00)
    // outputs standard I2S stereo (mic1=L, mic2=R).
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK_PIN,
            .bclk = I2S_SCLK_PIN,
            .ws = I2S_LRCK_PIN,
            .dout = I2S_DOUT_PIN,
            .din = I2S_DIN_PIN,
            .invert_flags = { false, false, false },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &std_cfg));

    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));

    // Init audio codecs (I2C + codec registers) — input first (creates I2C bus)
    audio_input_init(rx_chan);
    audio_output_init(tx_chan);

    // Configure button (BOOT = push-to-talk)
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);

    // Allocate recording buffer in PSRAM (max 10s @ 16kHz 16-bit mono = 320KB)
    size_t max_samples = SAMPLE_RATE * RECORD_SECONDS;
    int16_t *rec_buffer = heap_caps_malloc(max_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!rec_buffer) {
        ESP_LOGE(TAG, "Failed to allocate recording buffer");
        leds_set_state(LED_ERROR);
        return;
    }

    // Startup beep (440Hz, 200ms)
    {
        int beep_samples = SAMPLE_RATE / 5;  // 200ms
        int16_t *beep = heap_caps_malloc(beep_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
        if (beep) {
            for (int i = 0; i < beep_samples; i++) {
                beep[i] = (int16_t)(8000.0f * sinf(2.0f * M_PI * 440.0f * i / SAMPLE_RATE));
            }
            audio_output_play(beep, beep_samples);
            heap_caps_free(beep);
        }
    }

    leds_set_state(LED_IDLE);
    ESP_LOGI(TAG, "Ready. Press BOOT button to talk.");

    while (1) {
        // Wait for button press (active low)
        if (gpio_get_level(BUTTON_PIN) == 0) {
            // Debounce
            vTaskDelay(pdMS_TO_TICKS(50));
            if (gpio_get_level(BUTTON_PIN) != 0) continue;

            // ── Record ──────────────────────────────────────────
            ESP_LOGI(TAG, "Recording...");
            leds_set_state(LED_LISTENING);

            size_t recorded = audio_input_record(rec_buffer, max_samples);
            if (recorded == 0) {
                ESP_LOGW(TAG, "No audio recorded");
                leds_set_state(LED_IDLE);
                continue;
            }

            // ── Send to ErinOS ──────────────────────────────────
            ESP_LOGI(TAG, "Sending to ErinOS...");
            leds_set_state(LED_THINKING);

            uint8_t *response = NULL;
            size_t response_size = 0;
            esp_err_t err = voice_request(rec_buffer, recorded, &response, &response_size);

            if (err != ESP_OK || response_size <= WAV_HEADER_SIZE) {
                ESP_LOGE(TAG, "Voice request failed");
                leds_set_state(LED_ERROR);
                vTaskDelay(pdMS_TO_TICKS(1000));
                leds_set_state(LED_IDLE);
                if (response) heap_caps_free(response);
                continue;
            }

            // ── Play response ───────────────────────────────────
            ESP_LOGI(TAG, "Playing response...");
            leds_set_state(LED_SPEAKING);

            // Read sample rate from WAV header and reconfigure codec
            uint32_t wav_sample_rate = *(uint32_t *)(response + 24);  // offset 24 in WAV header
            ESP_LOGI(TAG, "WAV sample rate: %lu Hz", (unsigned long)wav_sample_rate);
            if (wav_sample_rate != SAMPLE_RATE) {
                audio_output_set_sample_rate(wav_sample_rate);
            }

            // Skip WAV header, play raw PCM
            int16_t *pcm = (int16_t *)(response + WAV_HEADER_SIZE);
            size_t pcm_samples = (response_size - WAV_HEADER_SIZE) / sizeof(int16_t);
            audio_output_play(pcm, pcm_samples);

            // Restore recording sample rate
            if (wav_sample_rate != SAMPLE_RATE) {
                audio_output_set_sample_rate(SAMPLE_RATE);
            }

            heap_caps_free(response);
            leds_set_state(LED_IDLE);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
