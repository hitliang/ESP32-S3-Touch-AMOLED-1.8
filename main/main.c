/*
 * main.c - ESP32-S3-Touch-AMOLED-1.8 主程序
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "display.h"
#include "touch.h"
#include "tca9554.h"
#include "ui.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESP32-S3-Touch-AMOLED-1.8");
    ESP_LOGI(TAG, "  Build: %s %s", __DATE__, __TIME__);
    ESP_LOGI(TAG, "========================================");

    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // I2C bus (touch_init 里初始化)
    // 先初始化 TCA9554 需要先有 I2C 总线
    // 但是 touch.c 用的是 i2c_new_master_bus API (ESP-IDF 5.3)
    // 而 tca9554 还用旧 API —— 需要统一
    // 暂时跳过 TCA9554，直接用 display + touch

    // 显示驱动 + LVGL
    ESP_ERROR_CHECK(display_init());

    // 触摸驱动
    ESP_ERROR_CHECK(touch_init());

    // 创建 UI
    ui_create();

    ESP_LOGI(TAG, "System ready!");
}
