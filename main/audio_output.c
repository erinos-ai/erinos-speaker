#include "audio_output.h"
#include "config.h"

#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "audio_out";
static esp_codec_dev_handle_t codec_dev = NULL;

extern i2c_master_bus_handle_t i2c_bus;

esp_err_t audio_output_init(i2s_chan_handle_t tx_chan)
{
    // --- PA enable via TCA9555 IO expander ---
    esp_io_expander_handle_t io_expander = NULL;
    esp_io_expander_new_i2c_tca95xx_16bit(i2c_bus, 0x20, &io_expander);
    esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_8, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_8, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "Power amplifier enabled");

    // --- ES8311 codec via esp_codec_dev ---
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = 0,
        .rx_handle = NULL,
        .tx_handle = tx_chan,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = 0,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    es8311_codec_cfg_t es8311_cfg = {
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .pa_pin = -1,  // PA managed via IO expander, not direct GPIO
        .use_mclk = true,
    };
    es8311_cfg.hw_gain.pa_voltage = 5.0;
    es8311_cfg.hw_gain.codec_dac_voltage = 3.3;
    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_cfg);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = codec_if,
        .data_if = data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    codec_dev = esp_codec_dev_new(&dev_cfg);

    esp_codec_dev_sample_info_t sample_info = {
        .sample_rate = SAMPLE_RATE,
        .channel = 2,
        .bits_per_sample = 16,
    };
    esp_codec_dev_open(codec_dev, &sample_info);
    esp_codec_dev_set_out_vol(codec_dev, 80);

    ESP_LOGI(TAG, "Audio output ready (16kHz 16-bit stereo via esp_codec_dev)");
    return ESP_OK;
}

esp_err_t audio_output_set_sample_rate(uint32_t rate)
{
    esp_codec_dev_sample_info_t sample_info = {
        .sample_rate = rate,
        .channel = 2,
        .bits_per_sample = 16,
    };
    esp_codec_dev_close(codec_dev);
    esp_codec_dev_open(codec_dev, &sample_info);
    esp_codec_dev_set_out_vol(codec_dev, 80);
    ESP_LOGI(TAG, "Sample rate set to %lu Hz", (unsigned long)rate);
    return ESP_OK;
}

esp_err_t audio_output_play(const int16_t *data, size_t samples)
{
    // Convert mono to stereo: duplicate each sample for L+R channels
    size_t chunk_frames = 256;
    static int16_t stereo_buf[256 * 2];

    size_t offset = 0;
    while (offset < samples) {
        size_t n = chunk_frames;
        if (offset + n > samples) n = samples - offset;

        for (size_t i = 0; i < n; i++) {
            stereo_buf[i * 2] = data[offset + i];
            stereo_buf[i * 2 + 1] = data[offset + i];
        }

        esp_codec_dev_write(codec_dev, stereo_buf, n * 2 * sizeof(int16_t));
        offset += n;
    }

    ESP_LOGI(TAG, "Played %zu samples (%.1fs)", samples, (float)samples / SAMPLE_RATE);
    return ESP_OK;
}
