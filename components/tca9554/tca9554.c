/*
 * tca9554.c - TCA9554 GPIO 扩展器驱动 (共享 I2C 总线)
 */

#include "tca9554.h"
#include "pin_config.h"

#include "freertos/FreeRTOS.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "tca9554";

#define REG_INPUT   0x00
#define REG_OUTPUT  0x01
#define REG_CONFIG  0x03

static uint8_t output_state = 0x00;
static i2c_master_dev_handle_t dev_handle = NULL;

// 从 touch.c 获取共享的 I2C 总线
extern i2c_master_bus_handle_t touch_get_i2c_bus(void);

static esp_err_t tca9554_write_reg(uint8_t reg, uint8_t val)
{
    if (!dev_handle) return ESP_ERR_INVALID_STATE;
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev_handle, buf, 2, pdMS_TO_TICKS(100));
}

esp_err_t tca9554_init(void)
{
    ESP_LOGI(TAG, "Initializing TCA9554...");

    i2c_master_bus_handle_t bus = touch_get_i2c_bus();
    if (!bus) {
        ESP_LOGE(TAG, "I2C bus not initialized. Call touch_init() first.");
        return ESP_ERR_INVALID_STATE;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TCA9554_ADDR,
        .scl_speed_hz = 400000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add TCA9554 device: 0x%x", ret);
        return ret;
    }

    ret = tca9554_write_reg(REG_CONFIG, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 communication failed: 0x%x", ret);
        return ret;
    }

    tca9554_write_reg(REG_OUTPUT, 0x00);
    output_state = 0x00;

    ESP_LOGI(TAG, "TCA9554 initialized");
    return ESP_OK;
}

esp_err_t tca9554_set_pin(uint8_t pin, bool level)
{
    if (pin > 7) return ESP_ERR_INVALID_ARG;
    if (level) output_state |= (1 << pin);
    else output_state &= ~(1 << pin);
    return tca9554_write_reg(REG_OUTPUT, output_state);
}

esp_err_t tca9554_get_pin(uint8_t pin, bool *level)
{
    if (pin > 7 || !level) return ESP_ERR_INVALID_ARG;
    uint8_t reg = REG_INPUT, val;
    esp_err_t ret = i2c_master_transmit_receive(dev_handle, &reg, 1, &val, 1, pdMS_TO_TICKS(100));
    if (ret == ESP_OK) *level = (val & (1 << pin)) != 0;
    return ret;
}

esp_err_t tca9554_write_all(uint8_t value)
{
    output_state = value;
    return tca9554_write_reg(REG_OUTPUT, value);
}

esp_err_t tca9554_read_all(uint8_t *value)
{
    if (!value) return ESP_ERR_INVALID_ARG;
    uint8_t reg = REG_INPUT;
    return i2c_master_transmit_receive(dev_handle, &reg, 1, value, 1, pdMS_TO_TICKS(100));
}
