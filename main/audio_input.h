#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

esp_err_t audio_input_init(void);
size_t audio_input_record(int16_t *buffer, size_t max_samples);
