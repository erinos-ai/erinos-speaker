#pragma once

#include "esp_err.h"

typedef enum {
    LED_IDLE,       // Soft breathing white
    LED_LISTENING,  // Pulsing pink
    LED_THINKING,   // Spinning pink
    LED_SPEAKING,   // Solid green
    LED_ERROR,      // Red flash
    LED_OFF,
} led_state_t;

esp_err_t leds_init(void);
void leds_set_state(led_state_t state);
