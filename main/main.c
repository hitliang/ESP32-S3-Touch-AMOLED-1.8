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

// TCA9554 LCD 控制引脚
#define TCA_LCD_PWR_EN    1
#define TCA_LCD_RESET     0
#define TCA_TOUCH_RESET   2

// 全局 I2C 总线句柄
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

    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    printf("[OK] NVS initialized\n");

    // 1. 创建 I2C 总线
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
    if (ret != ESP_OK) {
        printf("[!!] I2C bus init FAILED: 0x%x\n", ret);
    } else {
        printf("[OK] I2C bus initialized\n");
    }

    // 2. TCA9554 GPIO 扩展器（控制 LCD 电源和触摸复位）
    printf("[..] Initializing TCA9554...\n");
    ret = tca9554_init();
    if (ret != ESP_OK) {
        printf("[!!] TCA9554 init FAILED: 0x%x\n", ret);
    } else {
        printf("[OK] TCA9554 initialized\n");

        // 复位触摸芯片
        tca9554_set_pin(TCA_TOUCH_RESET, false);
        vTaskDelay(pdMS_TO_TICKS(20));
        tca9554_set_pin(TCA_TOUCH_RESET, true);
        vTaskDelay(pdMS_TO_TICKS(100));
        printf("[OK] Touch reset via TCA9554\n");

        // LCD 电源使能 & 复位
        tca9554_set_pin(TCA_LCD_PWR_EN, true);
        vTaskDelay(pdMS_TO_TICKS(10));
        tca9554_set_pin(TCA_LCD_RESET, true);
        vTaskDelay(pdMS_TO_TICKS(20));
        printf("[OK] LCD power enabled\n");
    }

    // 3. 触摸驱动（I2C 总线已创建，触摸芯片已复位）
    printf("[..] Initializing touch...\n");
    ret = touch_init();
    if (ret != ESP_OK) {
        printf("[!!] Touch init FAILED: 0x%x (non-fatal)\n", ret);
    } else {
        printf("[OK] Touch initialized\n");
    }

    // 4. 显示驱动 + LVGL
    printf("[..] Initializing display...\n");
    ret = display_init();
    if (ret != ESP_OK) {
        printf("[!!] Display init FAILED: 0x%x\n", ret);
    } else {
        printf("[OK] Display initialized\n");
    }

    // 5. 创建 UI
    ui_create();
    printf("[OK] UI created\n");

    printf("\n========================================\n");
    printf("  System ready!\n");
    printf("========================================\n\n");
}
