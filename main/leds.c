#include "leds.h"
#include "config.h"

#include "led_strip.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "leds";
static led_strip_handle_t strip = NULL;
static volatile led_state_t current_state = LED_OFF;
static TaskHandle_t led_task_handle = NULL;

static void set_all(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < LED_COUNT; i++) {
        led_strip_set_pixel(strip, i, r, g, b);
    }
    led_strip_refresh(strip);
}

static void led_task(void *arg)
{
    int tick = 0;
    while (1) {
        switch (current_state) {
        case LED_IDLE: {
            // Soft breathing white
            float brightness = (sinf(tick * 0.05f) + 1.0f) / 2.0f;
            uint8_t v = (uint8_t)(brightness * 15);
            set_all(v, v, v);
            break;
        }
        case LED_LISTENING: {
            // Pulsing pink
            float brightness = (sinf(tick * 0.15f) + 1.0f) / 2.0f;
            uint8_t v = (uint8_t)(brightness * 40 + 10);
            set_all(v, 0, v / 3);
            break;
        }
        case LED_THINKING: {
            // Spinning pink dot
            int active = (tick / 3) % LED_COUNT;
            for (int i = 0; i < LED_COUNT; i++) {
                if (i == active) {
                    led_strip_set_pixel(strip, i, 40, 0, 15);
                } else {
                    led_strip_set_pixel(strip, i, 3, 0, 1);
                }
            }
            led_strip_refresh(strip);
            break;
        }
        case LED_SPEAKING:
            set_all(0, 30, 0);
            break;
        case LED_ERROR:
            if ((tick / 5) % 2) {
                set_all(40, 0, 0);
            } else {
                set_all(0, 0, 0);
            }
            break;
        case LED_OFF:
        default:
            set_all(0, 0, 0);
            break;
        }
        tick++;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

esp_err_t leds_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_PIN,
        .max_leds = LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &strip));

    set_all(0, 0, 0);

    xTaskCreate(led_task, "leds", 2048, NULL, 2, &led_task_handle);
    ESP_LOGI(TAG, "LEDs initialized (%d pixels)", LED_COUNT);
    return ESP_OK;
}

void leds_set_state(led_state_t state)
{
    current_state = state;
}
