#include "ui_home.h"
#include "sys_battery.h"
#include "sys_wifi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <time.h>

static const char *TAG = "ui_home";

static lv_obj_t *lbl_clock = NULL;
static lv_obj_t *lbl_date  = NULL;
static lv_obj_t *lbl_batt  = NULL;
static lv_obj_t *lbl_wifi  = NULL;
static lv_obj_t *lbl_hint  = NULL;

lv_obj_t *ui_home_create(lv_obj_t *scr)
{
    /* Dark background */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Clock - large */
    lv_obj_t *clock = lv_label_create(scr);
    lv_obj_set_style_text_font(clock, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(clock, lv_color_hex(0xeeeeee), 0);
    lv_label_set_text(clock, "00:00");
    lv_obj_align(clock, LV_ALIGN_CENTER, 0, -40);
    lbl_clock = clock;

    /* Date */
    lv_obj_t *date = lv_label_create(scr);
    lv_obj_set_style_text_font(date, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(date, lv_color_hex(0xaaaaaa), 0);
    lv_label_set_text(date, "2026/01/01");
    lv_obj_align(date, LV_ALIGN_CENTER, 0, 10);
    lbl_date = date;

    /* Battery - top right */
    lv_obj_t *batt = lv_label_create(scr);
    lv_obj_set_style_text_font(batt, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(batt, lv_color_hex(0x44cc44), 0);
    lv_label_set_text(batt, LV_SYMBOL_BATTERY_FULL " --%");
    lv_obj_align(batt, LV_ALIGN_TOP_RIGHT, -10, 10);
    lbl_batt = batt;

    /* WiFi - top left */
    lv_obj_t *wifi = lv_label_create(scr);
    lv_obj_set_style_text_font(wifi, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(wifi, lv_color_hex(0x888888), 0);
    lv_label_set_text(wifi, LV_SYMBOL_WIFI " --");
    lv_obj_align(wifi, LV_ALIGN_TOP_LEFT, 10, 10);
    lbl_wifi = wifi;

    /* Swipe hint - bottom */
    lv_obj_t *hint = lv_label_create(scr);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555566), 0);
    lv_label_set_text(hint, LV_SYMBOL_UP " Swipe up for Apps  " LV_SYMBOL_UP);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);
    lbl_hint = hint;

    ESP_LOGI(TAG, "Home screen created");
    return scr;
}

void ui_home_update(void)
{
    /* Update clock and date */
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    if (lbl_clock) {
        lv_label_set_text_fmt(lbl_clock, "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
    }
    if (lbl_date) {
        lv_label_set_text_fmt(lbl_date, "%04d/%02d/%02d",
                              tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday);
    }

    /* Battery */
    if (lbl_batt) {
        uint8_t pct = sys_battery_get_percent();
        lv_label_set_text_fmt(lbl_batt, "%s %d%%",
            pct > 20 ? LV_SYMBOL_BATTERY_FULL :
            pct > 10 ? LV_SYMBOL_BATTERY_2 : LV_SYMBOL_BATTERY_EMPTY,
            pct);
    }

    /* WiFi */
    if (lbl_wifi) {
        wifi_status_t ws = sys_wifi_get_status();
        if (ws == WIFI_CONNECTED && sys_time_is_synced()) {
            lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0x44cc44), 0);
        } else if (ws == WIFI_CONNECTING) {
            lv_label_set_text(lbl_wifi, LV_SYMBOL_REFRESH);
            lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0xccaa44), 0);
        } else {
            lv_label_set_text(lbl_wifi, LV_SYMBOL_CLOSE);
            lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0xcc4444), 0);
        }
    }
}
