/*
 * ui.c - LVGL 9 - 先显示一个简单的红色屏幕确认能亮
 */

#include "ui.h"
#include "display.h"
#include "esp_log.h"

static const char *TAG = "ui";

void ui_create(void)
{
    ESP_LOGI(TAG, "Creating simple UI...");

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFF0000), 0);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Hello ESP32!");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_center(label);

    ESP_LOGI(TAG, "UI created");
}

void ui_update_time(const char *time_str)
{
    (void)time_str;
}

void ui_update_wifi_status(bool connected, const char *ssid)
{
    (void)connected;
    (void)ssid;
}
