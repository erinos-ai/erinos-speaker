#pragma once

#include "driver/i2s_std.h"
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

esp_err_t audio_input_init(i2s_chan_handle_t rx_chan);
size_t audio_input_record(int16_t *buffer, size_t max_samples);
