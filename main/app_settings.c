#include "app_settings.h"
#include "sys_wifi.h"
#include "sys_battery.h"
#include "lvgl.h"
#include "esp_system.h"

static lv_obj_t *root = NULL;

static lv_obj_t *add_line(const char *text, int y)
{
    lv_obj_t *l = lv_label_create(root);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xcccccc), 0);
    lv_obj_set_pos(l, 15, y);
    return l;
}

static lv_obj_t *add_section(const char *text, int y)
{
    lv_obj_t *l = lv_label_create(root);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x8888cc), 0);
    lv_obj_set_pos(l, 10, y);
    return l;
}

static void create(lv_obj_t *parent)
{
    root = lv_obj_create(parent);
    lv_obj_set_size(root, 340, 390);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_scroll_dir(root, LV_DIR_VER);

    int y = 5;

    /* Status section */
    add_section("Status", y); y += 30;

    wifi_status_t ws = sys_wifi_get_status();
    const char *ws_str = (ws == WIFI_CONNECTED) ? "Connected" :
                         (ws == WIFI_CONNECTING) ? "Connecting..." : "Offline";
    add_line(ws_str, y); y += 25;
    add_line("SSID: BABY&L", y); y += 25;
    add_line("Battery: ??%", y); y += 25;

    /* Battery value - update separately since we have the actual value */
    y -= 25;
    {
        lv_obj_t *l = lv_label_create(root);
        lv_label_set_text_fmt(l, "Battery: %d%%", sys_battery_get_percent());
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0xcccccc), 0);
        lv_obj_set_pos(l, 15, y);
    }
    y += 35;

    /* About section */
    add_section("About", y); y += 30;
    add_line("ESP-IDF v5.4", y); y += 25;
    add_line("LVGL 8.4.0", y); y += 25;
    add_line("SH8601 368x448 AMOLED", y); y += 25;
    add_line("ESP32-S3R8 16MB", y); y += 35;

    /* Network section */
    add_section("Network", y); y += 30;
    add_line("NTP: ntp.aliyun.com", y); y += 25;
    add_line("TZ: CST-8 (UTC+8)", y); y += 30;
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
