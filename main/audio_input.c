#include "audio_input.h"
#include "config.h"

#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "audio_in";
static i2s_chan_handle_t rx_chan = NULL;

// ─── ES7210 register setup (minimal for 16kHz mono capture) ────────
i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t es7210_dev = NULL;

static esp_err_t es7210_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(es7210_dev, buf, 2, 100);
}

static esp_err_t es7210_init(void)
{
    // Reset
    es7210_write_reg(0x00, 0xFF);
    vTaskDelay(pdMS_TO_TICKS(20));
    es7210_write_reg(0x00, 0x41);

    // Clock: MCLK from I2S master
    es7210_write_reg(0x01, 0x20);  // CLK ON
    es7210_write_reg(0x02, 0xC1);  // MCLK -> internal
    es7210_write_reg(0x03, 0x04);  // MCLK/LRCK ratio for 16kHz
    es7210_write_reg(0x04, 0x01);  // MCLK pre-div
    es7210_write_reg(0x05, 0x00);  // ADC OSR

    // I2S format: 16-bit, standard
    es7210_write_reg(0x11, 0x60);  // SDP format
    es7210_write_reg(0x06, 0x00);  // TDM off

    // ADC enable: channel 1 only (mono)
    es7210_write_reg(0x07, 0x20);  // Power up ADC1
    es7210_write_reg(0x08, 0x10);  // Power management

    // Analog: MIC1 PGA gain
    es7210_write_reg(0x43, 0x1E);  // +30dB gain
    es7210_write_reg(0x44, 0x1E);
    es7210_write_reg(0x45, 0x00);
    es7210_write_reg(0x46, 0x00);

    // Power up analog
    es7210_write_reg(0x40, 0x42);
    es7210_write_reg(0x41, 0x70);
    es7210_write_reg(0x42, 0x00);

    ESP_LOGI(TAG, "ES7210 initialized");
    return ESP_OK;
}

esp_err_t audio_input_init(void)
{
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

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ES7210_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &es7210_dev));

    es7210_init();

    // I2S RX channel (microphone)
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_chan));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_MCLK_PIN,
            .bclk = I2S_SCLK_PIN,
            .ws = I2S_LRCK_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_DIN_PIN,
            .invert_flags = { false, false, false },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));

    ESP_LOGI(TAG, "Audio input ready (16kHz 16-bit mono)");
    return ESP_OK;
}

size_t audio_input_record(int16_t *buffer, size_t max_samples)
{
    size_t total = 0;
    size_t bytes_read;
    size_t max_bytes = max_samples * sizeof(int16_t);

    while (total < max_bytes) {
        size_t chunk = 1024;
        if (total + chunk > max_bytes) chunk = max_bytes - total;

        esp_err_t ret = i2s_channel_read(rx_chan, (uint8_t *)buffer + total, chunk, &bytes_read, pdMS_TO_TICKS(100));
        if (ret != ESP_OK || bytes_read == 0) break;
        total += bytes_read;

        // Check if button released (stop recording)
        if (gpio_get_level(BUTTON_PIN) == 1) break;
    }

    ESP_LOGI(TAG, "Recorded %zu samples (%.1fs)", total / 2, (float)(total / 2) / SAMPLE_RATE);
    return total / sizeof(int16_t);
}
