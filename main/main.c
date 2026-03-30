/*
 * main.c - ESP32-S3-Touch-AMOLED-1.8
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

#include "pin_config.h"
#include "display.h"
#include "touch.h"
#include "tca9554.h"

#define TCA_LCD_PWR_EN    1
#define TCA_LCD_RESET     0
#define TCA_TOUCH_RESET   2

static i2c_master_bus_handle_t i2c_bus = NULL;

i2c_master_bus_handle_t get_i2c_bus(void)
{
    return i2c_bus;
}

void app_main(void)
{
    printf("\n\n========================================\n");
    printf("  ESP32-S3-Touch-AMOLED-1.8\n");
    printf("  Build: %s %s\n", __DATE__, __TIME__);
    printf("  Free heap: %lu bytes\n", (unsigned long)esp_get_free_heap_size());
    printf("========================================\n\n");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    printf("[OK] NVS\n");

    // I2C
    i2c_master_bus_config_t i2c_bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_SCL_PIN,
        .sda_io_num = I2C_SDA_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus));
    printf("[OK] I2C\n");

    // TCA9554
    ret = tca9554_init();
    if (ret == ESP_OK) {
        tca9554_set_pin(TCA_TOUCH_RESET, false);
        vTaskDelay(pdMS_TO_TICKS(20));
        tca9554_set_pin(TCA_TOUCH_RESET, true);
        vTaskDelay(pdMS_TO_TICKS(100));
        tca9554_set_pin(TCA_LCD_PWR_EN, true);
        vTaskDelay(pdMS_TO_TICKS(10));
        tca9554_set_pin(TCA_LCD_RESET, true);
        vTaskDelay(pdMS_TO_TICKS(20));
        printf("[OK] TCA9554 + LCD power\n");
    }

    // Display
    ESP_ERROR_CHECK(display_init());
    printf("[OK] Display\n");

    // Touch
    ret = touch_init();
    printf(ret == ESP_OK ? "[OK] Touch\n" : "[!!] Touch FAIL\n");

    // 等 LVGL task 跑起来
    vTaskDelay(pdMS_TO_TICKS(200));

    // 直接用 LVGL API 画个红色屏幕
    lv_obj_t *scr = lv_scr_act();
    if (scr) {
        lv_obj_set_style_bg_color(scr, lv_color_hex(0xFF0000), 0);
        lv_obj_t *label = lv_label_create(scr);
        lv_label_set_text(label, "Hello ESP32!");
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_center(label);
        printf("[OK] UI drawn\n");
    } else {
        printf("[!!] lv_scr_act() returned NULL!\n");
    }

    printf("\n========================================\n");
    printf("  System ready!\n");
    printf("========================================\n\n");
}
