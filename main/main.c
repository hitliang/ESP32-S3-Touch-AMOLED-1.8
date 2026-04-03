/*
 * main.c - ESP32-S3-Touch-AMOLED-1.8 UI Framework Demo
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#include "pin_config.h"
#include "display.h"
#include "touch.h"
#include "tca9554.h"
#include "ui_framework.h"

static const char *TAG = "main";

static i2c_master_bus_handle_t i2c_bus = NULL;
static esp_timer_handle_t ui_timer = NULL;

i2c_master_bus_handle_t get_i2c_bus(void)
{
    return i2c_bus;
}

// ==================== 应用回调函数 ====================

static void app_wifi_callback(void)
{
    ESP_LOGI(TAG, "WiFi app clicked - Open WiFi settings");
    // TODO: 打开 WiFi 设置界面
}

static void app_bluetooth_callback(void)
{
    ESP_LOGI(TAG, "Bluetooth app clicked - Open Bluetooth settings");
    // TODO: 打开蓝牙设置界面
}

static void app_music_callback(void)
{
    ESP_LOGI(TAG, "Music app clicked - Open music player");
    // TODO: 打开音乐播放器
}

static void app_sensor_callback(void)
{
    ESP_LOGI(TAG, "Sensor app clicked - Show sensor data");
    // TODO: 显示传感器数据（QMI8658 六轴传感器）
}

static void app_clock_callback(void)
{
    ESP_LOGI(TAG, "Clock app clicked - Open clock/alarm");
    // TODO: 打开时钟/闹钟应用
}

static void app_settings_callback(void)
{
    ESP_LOGI(TAG, "Settings app clicked - Open system settings");
    // TODO: 打开系统设置
}

// ==================== UI 定时器回调 ====================

static void ui_timer_callback(void *arg)
{
    static uint8_t seconds = 0;
    static uint8_t last_minute = 0;
    
    seconds++;
    
    // 每分钟更新时间
    if (seconds % 60 == 0) {
        uint8_t minute = (last_minute + 1) % 60;
        uint8_t hour = (minute == 0) ? (last_minute / 60 + 1) % 24 : last_minute / 60;
        
        if (display_lock(100)) {
            ui_update_time(hour, minute);
            display_unlock();
        }
        
        last_minute = minute;
        
        // 模拟电量变化（实际应从 AXP2101 读取）
        static uint8_t battery = 85;
        if (seconds % 300 == 0 && battery > 0) {  // 每 5 分钟减少 1%
            battery--;
            if (display_lock(100)) {
                ui_update_battery(battery);
                display_unlock();
            }
        }
    }
}

// ==================== 初始化应用回调 ====================

static void init_app_callbacks(void)
{
    ui_register_app_callback(UI_APP_WIFI, app_wifi_callback);
    ui_register_app_callback(UI_APP_BLUETOOTH, app_bluetooth_callback);
    ui_register_app_callback(UI_APP_MUSIC, app_music_callback);
    ui_register_app_callback(UI_APP_SENSOR, app_sensor_callback);
    ui_register_app_callback(UI_APP_CLOCK, app_clock_callback);
    ui_register_app_callback(UI_APP_SETTINGS, app_settings_callback);
}

// ==================== 初始化 UI 定时器 ====================

static void init_ui_timer(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = ui_timer_callback,
        .name = "ui_timer"
    };
    
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &ui_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(ui_timer, 1000000));  // 1 秒
    ESP_LOGI(TAG, "UI timer started");
}

void app_main(void)
{
    printf("\n\n========================================\n");
    printf("  ESP32-S3-Touch-AMOLED-1.8\n");
    printf("  UI Framework Demo\n");
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

    // 等待 LVGL task 启动
    vTaskDelay(pdMS_TO_TICKS(200));

    // 初始化 UI 框架
    printf("\nInitializing UI framework...\n");
    ret = ui_framework_init();
    if (ret == ESP_OK) {
        printf("[OK] UI Framework\n");
    } else {
        printf("[!!] UI Framework FAIL\n");
    }

    // 注册应用回调
    init_app_callbacks();
    printf("[OK] App callbacks registered\n");

    // 启动 UI 定时器
    init_ui_timer();

    printf("\n========================================\n");
    printf("  System ready!\n");
    printf("  Touch the app icons to interact\n");
    printf("========================================\n\n");
}
