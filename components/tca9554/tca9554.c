/*
 * tca9554.c - TCA9554 GPIO 扩展器驱动
 */

#include "tca9554.h"
#include "pin_config.h"

#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "tca9554";

#define I2C_PORT  I2C_NUM_0

// TCA9554 寄存器
#define REG_INPUT   0x00
#define REG_OUTPUT  0x01
#define REG_POLARITY 0x02
#define REG_CONFIG  0x03

static uint8_t output_state = 0x00;

static esp_err_t tca9554_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_write_to_device(I2C_PORT, TCA9554_ADDR, buf, 2, pdMS_TO_TICKS(100));
}

static esp_err_t tca9554_read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_master_write_to_device(I2C_PORT, TCA9554_ADDR, &reg, 1, pdMS_TO_TICKS(100));
    // Then read
    return i2c_master_read_from_device(I2C_PORT, TCA9554_ADDR, val, 1, pdMS_TO_TICKS(100));
}

esp_err_t tca9554_init(void)
{
    ESP_LOGI(TAG, "Initializing TCA9554...");

    // 所有引脚设为输出
    esp_err_t ret = tca9554_write_reg(REG_CONFIG, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 communication failed");
        return ret;
    }

    // 初始输出全低
    tca9554_write_reg(REG_OUTPUT, 0x00);
    output_state = 0x00;

    ESP_LOGI(TAG, "TCA9554 initialized");
    return ESP_OK;
}

esp_err_t tca9554_set_pin(uint8_t pin, bool level)
{
    if (pin > 7) return ESP_ERR_INVALID_ARG;
    if (level) {
        output_state |= (1 << pin);
    } else {
        output_state &= ~(1 << pin);
    }
    return tca9554_write_reg(REG_OUTPUT, output_state);
}

esp_err_t tca9554_get_pin(uint8_t pin, bool *level)
{
    if (pin > 7 || !level) return ESP_ERR_INVALID_ARG;
    uint8_t input_val;
    esp_err_t ret = i2c_master_read_from_device(I2C_PORT, TCA9554_ADDR, &input_val, 1, pdMS_TO_TICKS(100));
    if (ret == ESP_OK) {
        *level = (input_val & (1 << pin)) != 0;
    }
    return ret;
}

esp_err_t tca9554_write_all(uint8_t value)
{
    output_state = value;
    return tca9554_write_reg(REG_OUTPUT, value);
}

esp_err_t tca9554_read_all(uint8_t *value)
{
    return i2c_master_read_from_device(I2C_PORT, TCA9554_ADDR, value, 1, pdMS_TO_TICKS(100));
}
