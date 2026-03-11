#include "audio_output.h"
#include "config.h"

#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "audio_out";
static i2s_chan_handle_t tx_chan = NULL;

// ─── ES8311 register setup (minimal for 16kHz mono playback) ───────
extern i2c_master_bus_handle_t i2c_bus;  // Shared with audio_input
static i2c_master_dev_handle_t es8311_dev = NULL;

static esp_err_t es8311_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(es8311_dev, buf, 2, 100);
}

static esp_err_t es8311_codec_init(void)
{
    // Reset
    es8311_write_reg(0x00, 0x1F);
    vTaskDelay(pdMS_TO_TICKS(20));
    es8311_write_reg(0x00, 0x00);

    // Clock configuration
    es8311_write_reg(0x01, 0x30);  // CLK manager: auto MCLK
    es8311_write_reg(0x02, 0x10);  // MCLK divider
    es8311_write_reg(0x03, 0x10);  // ADC/DAC clock
    es8311_write_reg(0x16, 0x24);  // ADC clock
    es8311_write_reg(0x04, 0x10);  // DAC clock
    es8311_write_reg(0x05, 0x00);  // CLK manager

    // I2S format: 16-bit, standard Philips
    es8311_write_reg(0x09, 0x00);  // SDP in format
    es8311_write_reg(0x0A, 0x00);  // SDP out format

    // System control
    es8311_write_reg(0x0B, 0x00);  // System
    es8311_write_reg(0x0C, 0x00);  // System
    es8311_write_reg(0x10, 0x1F);  // System
    es8311_write_reg(0x11, 0x7F);  // System

    // DAC
    es8311_write_reg(0x12, 0x00);  // DAC control
    es8311_write_reg(0x13, 0x10);  // ADC/DAC config
    es8311_write_reg(0x1C, 0x6A);  // ADC EQ
    es8311_write_reg(0x37, 0x08);  // ADC ramp rate

    // DAC volume
    es8311_write_reg(0x32, 0xBF);  // DAC volume (~-6dB)

    // Power up
    es8311_write_reg(0x14, 0x8A);  // Analog control
    es8311_write_reg(0x15, 0x00);  // Analog control

    // Unmute DAC
    es8311_write_reg(0x31, 0x00);

    ESP_LOGI(TAG, "ES8311 initialized");
    return ESP_OK;
}

esp_err_t audio_output_init(void)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ES8311_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &es8311_dev));

    es8311_codec_init();

    // I2S TX channel (speaker)
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_MCLK_PIN,
            .bclk = I2S_SCLK_PIN,
            .ws = I2S_LRCK_PIN,
            .dout = I2S_DOUT_PIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = { false, false, false },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));

    ESP_LOGI(TAG, "Audio output ready (16kHz 16-bit mono)");
    return ESP_OK;
}

esp_err_t audio_output_play(const int16_t *data, size_t samples)
{
    size_t bytes_written;
    size_t total = samples * sizeof(int16_t);
    size_t offset = 0;

    while (offset < total) {
        size_t chunk = 1024;
        if (offset + chunk > total) chunk = total - offset;

        esp_err_t ret = i2s_channel_write(tx_chan, (const uint8_t *)data + offset, chunk, &bytes_written, pdMS_TO_TICKS(1000));
        if (ret != ESP_OK) return ret;
        offset += bytes_written;
    }

    ESP_LOGI(TAG, "Played %zu samples (%.1fs)", samples, (float)samples / SAMPLE_RATE);
    return ESP_OK;
}
