/*
 * touch.c - FT3168 触摸驱动 (LVGL 9, 共享 I2C 总线)
 */

#include "touch.h"
#include "display.h"
#include "pin_config.h"

#include "freertos/FreeRTOS.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_lvgl_port.h"

static const char *TAG = "touch";

// 从 main.c 获取共享 I2C 总线
extern i2c_master_bus_handle_t get_i2c_bus(void);

esp_err_t touch_init(void)
{
    i2c_master_bus_handle_t bus = get_i2c_bus();
    if (!bus) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // FT5x06 I2C IO 配置
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    esp_err_t ret = esp_lcd_new_panel_io_i2c(bus, &tp_io_cfg, &tp_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create touch IO: 0x%x", ret);
        return ret;
    }

    // 触摸驱动
    esp_lcd_touch_handle_t tp_handle = NULL;
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_WIDTH,
        .y_max = LCD_HEIGHT,
        .rst_gpio_num = -1,  // 复位通过 TCA9554
        .int_gpio_num = TP_INT_PIN,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    ret = esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FT5x06 init failed: 0x%x", ret);
        return ret;
    }

    // LVGL port
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = display_get(),
        .handle = tp_handle,
    };
    lv_indev_t *touch_indev = lvgl_port_add_touch(&touch_cfg);
    if (!touch_indev) {
        ESP_LOGE(TAG, "Failed to add touch to LVGL port");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "FT3168 touch initialized");
    return ESP_OK;
}
