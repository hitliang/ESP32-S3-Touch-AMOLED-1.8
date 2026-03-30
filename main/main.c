/*
 * main.c - ESP32-S3-Touch-AMOLED-1.8 主程序
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "display.h"
#include "touch.h"
#include "tca9554.h"
#include "ui.h"

static const char *TAG = "main";

// TCA9554 LCD 控制引脚
#define TCA_LCD_PWR_EN    1
#define TCA_LCD_RESET     0

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

    // 1. 先初始化触摸（创建 I2C 总线）
    printf("[..] Initializing touch (I2C)...\n");
    ret = touch_init();
    if (ret != ESP_OK) {
        printf("[!!] Touch init FAILED: 0x%x\n", ret);
    } else {
        printf("[OK] Touch initialized\n");
    }

    // 2. TCA9554 GPIO 扩展器（共享 I2C 总线）
    printf("[..] Initializing TCA9554...\n");
    ret = tca9554_init();
    if (ret != ESP_OK) {
        printf("[!!] TCA9554 init FAILED: 0x%x\n", ret);
    } else {
        printf("[OK] TCA9554 initialized\n");

        // LCD 电源使能 & 复位
        tca9554_set_pin(TCA_LCD_PWR_EN, true);
        vTaskDelay(pdMS_TO_TICKS(10));
        tca9554_set_pin(TCA_LCD_RESET, true);
        vTaskDelay(pdMS_TO_TICKS(20));
        printf("[OK] LCD power enabled\n");
    }

    // 3. 显示驱动 + LVGL
    printf("[..] Initializing display...\n");
    ret = display_init();
    if (ret != ESP_OK) {
        printf("[!!] Display init FAILED: 0x%x\n", ret);
    } else {
        printf("[OK] Display initialized\n");
    }

    // 4. 创建 UI
    ui_create();
    printf("[OK] UI created\n");

    printf("\n========================================\n");
    printf("  System ready!\n");
    printf("========================================\n\n");
}
