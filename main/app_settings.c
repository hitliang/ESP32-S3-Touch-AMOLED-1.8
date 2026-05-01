#include "app_settings.h"
#include "sys_wifi.h"
#include "sys_battery.h"
#include "lvgl.h"
#include "esp_system.h"
#include <stdio.h>

static lv_obj_t *root = NULL;

static void create(lv_obj_t *parent)
{
    root = lv_obj_create(parent);
    lv_obj_set_size(root, 340, 370);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_scroll_dir(root, LV_DIR_VER);

    int y = 5;
    char buf[128];

    /* WiFi Status */
    wifi_status_t ws = sys_wifi_get_status();
    const char *wifi_str = (ws == WIFI_CONNECTED) ? "Connected" :
                           (ws == WIFI_CONNECTING) ? "Connecting..." : "Offline";

    lv_obj_t *l = lv_label_create(root);
    lv_label_set_text_fmt(l, "WiFi: %s", wifi_str);
    lv_obj_set_pos(l, 10, y); y += 25;

    l = lv_label_create(root);
    lv_label_set_text_fmt(l, "SSID: BABY&L");
    lv_obj_set_pos(l, 10, y); y += 20;

    /* Battery */
    l = lv_label_create(root);
    lv_label_set_text_fmt(l, "Battery: %d%%", sys_battery_get_percent());
    lv_obj_set_pos(l, 10, y); y += 30;

    /* Divider */
    lv_obj_t *div = lv_obj_create(root);
    lv_obj_set_size(div, 320, 1);
    lv_obj_set_pos(div, 10, y);
    lv_obj_set_style_bg_color(div, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(div, 0, 0);
    y += 15;

    /* System info */
    l = lv_label_create(root);
    lv_label_set_text(l, "About");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x8888cc), 0);
    lv_obj_set_pos(l, 10, y); y += 25;

    l = lv_label_create(root);
    lv_label_set_text_fmt(l, "Firmware: ESP-IDF v5.4");
    lv_obj_set_pos(l, 10, y); y += 20;

    l = lv_label_create(root);
    lv_label_set_text_fmt(l, "Free heap: %lu KB", esp_get_free_heap_size() / 1024);
    lv_obj_set_pos(l, 10, y); y += 20;

    l = lv_label_create(root);
    lv_label_set_text(l, "Board: ESP32-S3-Touch-AMOLED-1.8");
    lv_obj_set_pos(l, 10, y); y += 20;

    l = lv_label_create(root);
    lv_label_set_text(l, "Display: SH8601 368x448 AMOLED");
    lv_obj_set_pos(l, 10, y); y += 20;

    l = lv_label_create(root);
    lv_label_set_text(l, "LVGL: 8.4.0");
    lv_obj_set_pos(l, 10, y); y += 30;

    /* Divider */
    div = lv_obj_create(root);
    lv_obj_set_size(div, 320, 1);
    lv_obj_set_pos(div, 10, y);
    lv_obj_set_style_bg_color(div, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(div, 0, 0);
    y += 15;

    /* WiFi credentials note */
    l = lv_label_create(root);
    lv_label_set_recolor(l, true);
    lv_label_set_text_fmt(l, "WiFi: BABY&L");
    lv_obj_set_pos(l, 10, y); y += 20;

    l = lv_label_create(root);
    lv_label_set_text(l, "NTP: ntp.aliyun.com  CST-8");
    lv_obj_set_pos(l, 10, y); y += 20;
}

static void destroy(void)
{
    if (root) { lv_obj_del(root); root = NULL; }
}

static void resume(void) {}

const app_entry_t app_settings = {
    .name = "Settings",
    .create = create, .destroy = destroy, .resume = resume,
};
