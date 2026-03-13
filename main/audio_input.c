#include "audio_input.h"
#include "config.h"

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es7210_adc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <stdlib.h>

static const char *TAG = "audio_in";
static i2s_chan_handle_t rx_chan = NULL;

// I2C bus shared with ES8311 (audio_output.c)
i2c_master_bus_handle_t i2c_bus = NULL;

esp_err_t audio_input_init(i2s_chan_handle_t shared_rx_chan)
{
    rx_chan = shared_rx_chan;

    // I2C bus (shared with ES8311 — init once)
    if (i2c_bus == NULL) {
        i2c_master_bus_config_t bus_cfg = {
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .i2c_port = I2C_NUM_0,
            .sda_io_num = I2C_SDA_PIN,
            .scl_io_num = I2C_SCL_PIN,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus));
    }

    // Use esp_codec_dev to properly initialize ES7210 registers.
    // We only use it for init — recording uses direct I2S reads
    // to avoid conflicts with the output codec dev on the same I2S bus.
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = 0,
        .addr = ES7210_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);

    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = ctrl_if,
        .master_mode = false,
        .mic_selected = ES7210_SEL_MIC1,
        .mclk_src = ES7210_MCLK_FROM_PAD,
    };
    const audio_codec_if_t *codec_if = es7210_codec_new(&es7210_cfg);
    if (!codec_if) {
        ESP_LOGE(TAG, "Failed to create ES7210 codec");
        return ESP_FAIL;
    }

    // Open codec to configure registers, then leave it — we read I2S directly
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = SAMPLE_RATE,
        .channel = 2,
        .bits_per_sample = 16,
    };
    int ret = codec_if->open(codec_if, &fs, sizeof(fs));
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to open ES7210: %d", ret);
        return ESP_FAIL;
    }
    // Set mic gain
    codec_if->set_mic_gain(codec_if, 30.0);

    ESP_LOGI(TAG, "Audio input ready (16kHz 16-bit via es7210 driver)");
    return ESP_OK;
}

size_t audio_input_record(int16_t *buffer, size_t max_samples)
{
    // ES7210 outputs stereo I2S (mic1=L, mic2=R).
    // Read stereo frames via direct I2S, extract left channel.
    #define STEREO_CHANNELS 2
    static int16_t stereo_buf[256 * STEREO_CHANNELS];
    size_t mono_samples = 0;
    size_t bytes_read;
    size_t min_samples = SAMPLE_RATE / 4;  // 250ms minimum

    // Debug: track energy per channel
    int64_t ch_energy[STEREO_CHANNELS] = {0};

    while (mono_samples < max_samples) {
        size_t frames_to_read = 256;
        if (mono_samples + frames_to_read > max_samples)
            frames_to_read = max_samples - mono_samples;

        size_t read_bytes = frames_to_read * STEREO_CHANNELS * sizeof(int16_t);
        esp_err_t ret = i2s_channel_read(rx_chan, stereo_buf, read_bytes, &bytes_read, pdMS_TO_TICKS(1000));
        if (ret != ESP_OK || bytes_read == 0) {
            ESP_LOGW(TAG, "I2S read stopped: ret=%d bytes_read=%zu", ret, bytes_read);
            break;
        }

        size_t frames_read = bytes_read / (STEREO_CHANNELS * sizeof(int16_t));
        for (size_t i = 0; i < frames_read; i++) {
            buffer[mono_samples + i] = stereo_buf[i * STEREO_CHANNELS];  // left channel
            for (int c = 0; c < STEREO_CHANNELS; c++) {
                int16_t v = stereo_buf[i * STEREO_CHANNELS + c];
                ch_energy[c] += (int64_t)v * v;
            }
        }
        mono_samples += frames_read;

        if (mono_samples >= min_samples && gpio_get_level(BUTTON_PIN) == 1) break;
    }

    ESP_LOGI(TAG, "Recorded %zu samples (%.1fs)", mono_samples, (float)mono_samples / SAMPLE_RATE);
    ESP_LOGI(TAG, "Channel energy: L=%lld R=%lld", ch_energy[0], ch_energy[1]);
    return mono_samples;
}
