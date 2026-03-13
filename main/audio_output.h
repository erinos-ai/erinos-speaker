#pragma once

#include "driver/i2s_std.h"
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

esp_err_t audio_output_init(i2s_chan_handle_t tx_chan);
esp_err_t audio_output_set_sample_rate(uint32_t rate);
esp_err_t audio_output_play(const int16_t *data, size_t samples);
