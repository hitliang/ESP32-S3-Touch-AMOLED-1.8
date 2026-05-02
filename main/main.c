#include "sys_i2c.h"
#include "sys_display.h"
#include "sys_touch.h"
#include "sys_battery.h"
#include "sys_wifi.h"
#include "sys_imu.h"
#include "sys_button.h"
#include "sys_config.h"
#include "app_framework.h"
#include "ui_home.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

static const char *TAG = "main";

static void gesture_handler(lv_dir_t dir)
{
    app_framework_handle_swipe(dir);
}

static bool screen_on = true;
static volatile int pending_action = 0;  /* 0=none, 1=go_home, 2=screen_off, 3=screen_on */

static void home_update_timer_cb(void *arg)
{
    sys_battery_update();

    if (sys_display_lock(200)) {
        /* Process pending button action */
        int act = pending_action;
        pending_action = 0;
        if (act == 1)       app_framework_go_home();
        else if (act == 2)  app_framework_screen_off();
        else if (act == 3)  app_framework_screen_on();

        /* Update home screen */
        if (screen_on && app_framework_get_state() == NAV_STATE_HOME) {
            ui_home_update();
        }
        sys_display_unlock();
    }
}

static void button_timer_cb(void *arg)
{
    if (!sys_button_poll()) return;

    /* Just set flag, let home_update_timer do the LVGL work safely */
    if (!screen_on) {
        screen_on = true;
        pending_action = 3;  /* screen_on */
    } else {
        nav_state_t st = app_framework_get_state();
        if (st == NAV_STATE_HOME) {
            screen_on = false;
            pending_action = 2;  /* screen_off */
        } else {
            pending_action = 1;  /* go_home */
        }
    }
}

void app_main(void)
{
    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    esp_log_level_set("FT5x06", ESP_LOG_NONE);
    esp_log_level_set("wifi", ESP_LOG_WARN);

    /* NVS must be first (needed by WiFi) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_LOGI(TAG, "=== Boot ===");

    /* 1. I2C bus + power sequencing */
    sys_i2c_init();

    /* 2. Display + LVGL */
    sys_display_init();
    sys_touch_init(sys_display_get_disp());
    sys_display_set_gesture_cb(gesture_handler);

    /* 3. UI framework */
    app_framework_init();
    app_framework_go_home();

    /* 4. System services (init after UI is stable) */
    sys_config_init();
    sys_battery_init();
    sys_imu_init();
    sys_button_init();

    /* 5. WiFi (async, starts connecting in background) */
    sys_wifi_init();

    /* 6. Periodic home update (every 1 second) */
    const esp_timer_create_args_t timer_args = {
        .callback = home_update_timer_cb,
        .name = "home_update",
    };
    esp_timer_handle_t timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 1000000));

    /* 7. Button polling timer (every 50ms) */
    const esp_timer_create_args_t btn_timer_args = {
        .callback = button_timer_cb,
        .name = "btn_poll",
    };
    esp_timer_handle_t btn_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&btn_timer_args, &btn_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(btn_timer, 50000));

    ESP_LOGI(TAG, "=== Boot complete ===");
}
