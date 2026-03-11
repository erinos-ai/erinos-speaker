#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

esp_err_t audio_output_init(void);
esp_err_t audio_output_play(const int16_t *data, size_t samples);
