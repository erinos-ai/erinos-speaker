#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

// Sends WAV audio to /api/voice and receives WAV response.
// response_buf must be pre-allocated. Returns actual response size.
esp_err_t voice_request(const int16_t *audio, size_t audio_samples,
                        uint8_t **response_buf, size_t *response_size);
