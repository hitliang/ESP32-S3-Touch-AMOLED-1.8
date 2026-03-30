/*
 * main.c - ESP32-S3-Touch-AMOLED-1.8 主程序
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
#include "ui.h"

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
    printf("[OK] NVS initialized\n");

    // 1. I2C 总线
    printf("[..] Initializing I2C bus...\n");
    i2c_master_bus_config_t i2c_bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_SCL_PIN,
        .sda_io_num = I2C_SDA_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ret = i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus);
    ESP_ERROR_CHECK(ret);
    printf("[OK] I2C bus initialized\n");

    // 2. TCA9554 - 控制 LCD 电源和触摸复位
    printf("[..] Initializing TCA9554...\n");
    ret = tca9554_init();
    if (ret == ESP_OK) {
        printf("[OK] TCA9554 initialized\n");
        // 复位触摸芯片
        tca9554_set_pin(TCA_TOUCH_RESET, false);
        vTaskDelay(pdMS_TO_TICKS(20));
        tca9554_set_pin(TCA_TOUCH_RESET, true);
        vTaskDelay(pdMS_TO_TICKS(100));
        // LCD 电源
        tca9554_set_pin(TCA_LCD_PWR_EN, true);
        vTaskDelay(pdMS_TO_TICKS(10));
        tca9554_set_pin(TCA_LCD_RESET, true);
        vTaskDelay(pdMS_TO_TICKS(20));
        printf("[OK] LCD power & touch reset done\n");
    } else {
        printf("[!!] TCA9554 FAILED: 0x%x\n", ret);
    }

    // 3. 显示驱动 + LVGL (必须在 touch 之前!)
    printf("[..] Initializing display...\n");
    ret = display_init();
    if (ret == ESP_OK) {
        printf("[OK] Display initialized\n");
    } else {
        printf("[!!] Display FAILED: 0x%x\n", ret);
    }

    // 4. 触摸驱动 (需要 display_get() 返回非 NULL)
    printf("[..] Initializing touch...\n");
    ret = touch_init();
    if (ret == ESP_OK) {
        printf("[OK] Touch initialized\n");
    } else {
        printf("[!!] Touch FAILED: 0x%x (non-fatal)\n", ret);
    }

    // 5. 创建 UI
    ui_create();
    printf("[OK] UI created\n");

    printf("\n========================================\n");
    printf("  System ready!\n");
    printf("========================================\n\n");
}
