#include "config.h"
#include "wifi.h"
#include "audio_input.h"
#include "audio_output.h"
#include "http_client.h"
#include "leds.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

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

    // Init audio hardware
    audio_input_init();
    audio_output_init();

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

            // Skip WAV header, play raw PCM
            int16_t *pcm = (int16_t *)(response + WAV_HEADER_SIZE);
            size_t pcm_samples = (response_size - WAV_HEADER_SIZE) / sizeof(int16_t);
            audio_output_play(pcm, pcm_samples);

            heap_caps_free(response);
            leds_set_state(LED_IDLE);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
