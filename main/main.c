#include "sys_i2c.h"
#include "sys_display.h"
#include "sys_touch.h"
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

static void home_update_timer_cb(void *arg)
{
    if (app_framework_get_state() == NAV_STATE_HOME) {
        if (sys_display_lock(100)) {
            ui_home_update();
            sys_display_unlock();
        }
    }
}

void app_main(void)
{
    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    esp_log_level_set("FT5x06", ESP_LOG_NONE);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_LOGI(TAG, "=== Boot ===");

    sys_i2c_init();
    sys_display_init();
    sys_touch_init(sys_display_get_disp());

    /* Register gesture handler */
    sys_display_set_gesture_cb(gesture_handler);

    app_framework_init();
    app_framework_go_home();

    /* Periodic home screen update (every 1s) */
    const esp_timer_create_args_t timer_args = {
        .callback = home_update_timer_cb,
        .name = "home_update",
    };
    esp_timer_handle_t timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 1000000));

    ESP_LOGI(TAG, "=== Boot complete ===");
}
