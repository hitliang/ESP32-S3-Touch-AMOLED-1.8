#include "ui_home.h"
#include "sys_battery.h"
#include "sys_wifi.h"
#include "audio_test.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <time.h>

static const char *TAG = "ui_home";

static lv_obj_t *lbl_clock = NULL;
static lv_obj_t *lbl_date  = NULL;
static lv_obj_t *lbl_batt  = NULL;
static lv_obj_t *lbl_wifi  = NULL;
static lv_obj_t *lbl_hint  = NULL;

/* Safe margins for rounded AMOLED corners */
#define SAFE_TOP     28
#define SAFE_SIDE    20
#define SAFE_BOTTOM  45

lv_obj_t *ui_home_create(lv_obj_t *scr)
{
    /* True AMOLED black background */
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* ---- Status bar: WiFi (top-left) ---- */
    lv_obj_t *wifi = lv_label_create(scr);
    lv_obj_set_style_text_font(wifi, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(wifi, lv_color_hex(0x888888), 0);
    lv_label_set_text(wifi, LV_SYMBOL_WIFI);
    lv_obj_align(wifi, LV_ALIGN_TOP_LEFT, SAFE_SIDE, SAFE_TOP);
    lbl_wifi = wifi;

    /* ---- Status bar: Battery (top-right) ---- */
    lv_obj_t *batt = lv_label_create(scr);
    lv_obj_set_style_text_font(batt, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(batt, lv_color_hex(0x44cc44), 0);
    lv_label_set_text(batt, LV_SYMBOL_BATTERY_FULL " --%");
    lv_obj_align(batt, LV_ALIGN_TOP_RIGHT, -SAFE_SIDE, SAFE_TOP);
    lbl_batt = batt;

    /* ---- Clock: large, glowing ---- */
    lv_obj_t *clock = lv_label_create(scr);
    lv_obj_set_style_text_font(clock, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(clock, lv_color_hex(0xffffff), 0);
    /* Soft cyan glow via label shadow */
    lv_obj_set_style_shadow_color(clock, lv_color_hex(0x3388cc), 0);
    lv_obj_set_style_shadow_width(clock, 20, 0);
    lv_obj_set_style_shadow_spread(clock, 4, 0);
    lv_obj_set_style_shadow_opa(clock, LV_OPA_70, 0);
    lv_label_set_text(clock, "00:00");
    lv_obj_align(clock, LV_ALIGN_CENTER, 0, -65);
    lbl_clock = clock;

    /* ---- Date: below clock ---- */
    lv_obj_t *date = lv_label_create(scr);
    lv_obj_set_style_text_font(date, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(date, lv_color_hex(0x777799), 0);
    lv_label_set_text(date, "2026/01/01 Wed");
    lv_obj_align(date, LV_ALIGN_CENTER, 0, 5);
    lbl_date = date;

    /* ---- Decorative accent line ---- */
    lv_obj_t *accent = lv_obj_create(scr);
    lv_obj_set_size(accent, 60, 2);
    lv_obj_set_style_bg_color(accent, lv_color_hex(0x334466), 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_align(accent, LV_ALIGN_CENTER, 0, 45);

    /* ---- Audio test button ---- */
    lv_obj_t *abtn = lv_btn_create(scr);
    lv_obj_set_size(abtn, 70, 30);
    lv_obj_align(abtn, LV_ALIGN_BOTTOM_LEFT, 10, -15);
    lv_obj_t *albl = lv_label_create(abtn);
    lv_label_set_text(albl, "Sound");
    lv_obj_center(albl);
    lv_obj_set_style_text_font(albl, &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(abtn, (lv_event_cb_t)audio_test_play, LV_EVENT_CLICKED, NULL);

    /* ---- Swipe hint: bottom, subtle ---- */
    lv_obj_t *hint = lv_label_create(scr);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x333344), 0);
    lv_label_set_text(hint, LV_SYMBOL_UP "  Swipe up for Apps  " LV_SYMBOL_UP);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -SAFE_BOTTOM);
    lbl_hint = hint;

    ESP_LOGI(TAG, "Home screen created");
    return scr;
}

void ui_home_update(void)
{
    /* Clock + date */
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    if (lbl_clock) {
        lv_label_set_text_fmt(lbl_clock, "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
    }
    if (lbl_date) {
        static const char *wd[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        lv_label_set_text_fmt(lbl_date, "%04d/%02d/%02d  %s",
            tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
            wd[tm_now.tm_wday]);
    }

    /* Battery */
    if (lbl_batt) {
        uint8_t pct = sys_battery_get_percent();
        const char *icon = LV_SYMBOL_BATTERY_FULL;
        uint32_t color = 0x44cc44;
        if (pct <= 10) { icon = LV_SYMBOL_BATTERY_EMPTY; color = 0xcc4444; }
        else if (pct <= 25) { icon = LV_SYMBOL_BATTERY_2; color = 0xccaa44; }
        else if (pct <= 50) { icon = LV_SYMBOL_BATTERY_3; color = 0xcccc44; }
        lv_label_set_text_fmt(lbl_batt, "%s %d%%", icon, pct);
        lv_obj_set_style_text_color(lbl_batt, lv_color_hex(color), 0);
    }

    /* WiFi */
    if (lbl_wifi) {
        wifi_status_t ws = sys_wifi_get_status();
        const char *icon;
        uint32_t color;
        if (ws == WIFI_CONNECTED && sys_time_is_synced()) {
            icon = LV_SYMBOL_WIFI;  color = 0x44cc44;
        } else if (ws == WIFI_CONNECTING) {
            icon = LV_SYMBOL_REFRESH;  color = 0xccaa44;
        } else {
            icon = LV_SYMBOL_CLOSE;  color = 0xcc4444;
        }
        lv_label_set_text(lbl_wifi, icon);
        lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(color), 0);
    }
}
