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

// 全局 I2C 总线句柄，由 touch_init 创建，供其他组件使用
static i2c_master_bus_handle_t shared_i2c_bus = NULL;

i2c_master_bus_handle_t touch_get_i2c_bus(void)
{
    return shared_i2c_bus;
}

esp_err_t touch_init(void)
{
    ESP_LOGI(TAG, "Initializing FT3168 touch controller...");

    // I2C 总线初始化
    i2c_master_bus_config_t i2c_bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_SCL_PIN,
        .sda_io_num = I2C_SDA_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&i2c_bus_cfg, &shared_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: 0x%x", ret);
        return ret;
    }

    // FT5x06 触摸驱动初始化
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(shared_i2c_bus, &tp_io_cfg, &tp_io_handle));

    esp_lcd_touch_handle_t tp_handle = NULL;
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_WIDTH,
        .y_max = LCD_HEIGHT,
        .rst_gpio_num = -1,  // 通过 TCA9554 控制复位
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
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp_handle));

    // 添加触摸到 LVGL port
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
