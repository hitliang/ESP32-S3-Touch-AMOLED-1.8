/*
 * main.c - ESP32-S3-Touch-AMOLED-1.8
 * v3: Visual polish + real WiFi scan + SNTP clock
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_sntp.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_io_expander_tca9554.h"
#include "lvgl.h"

static const char *TAG = "main";
static SemaphoreHandle_t lvgl_mux = NULL;

/* ── Hardware config ── */
#define LCD_HOST        SPI2_HOST
#define TOUCH_HOST      I2C_NUM_0
#define PIN_LCD_CS      GPIO_NUM_12
#define PIN_LCD_PCLK    GPIO_NUM_11
#define PIN_LCD_DATA0   GPIO_NUM_4
#define PIN_LCD_DATA1   GPIO_NUM_5
#define PIN_LCD_DATA2   GPIO_NUM_6
#define PIN_LCD_DATA3   GPIO_NUM_7
#define PIN_LCD_RST     (-1)
#define PIN_TOUCH_SCL   GPIO_NUM_14
#define PIN_TOUCH_SDA   GPIO_NUM_15
#define PIN_TOUCH_RST   (-1)
#define PIN_TOUCH_INT   GPIO_NUM_21

#define LCD_H_RES       368
#define LCD_V_RES       448
#define LCD_BIT_PER_PIXEL 16
#define LVGL_BUF_HEIGHT (LCD_V_RES / 4)
#define LVGL_TICK_PERIOD_MS 2
#define LVGL_TASK_PRIORITY 2
#define LVGL_TASK_STACK_SIZE (8 * 1024)

/* ── Color palette ── */
#define COLOR_BG          0x0A0E1A
#define COLOR_CARD        0x141B2D
#define COLOR_CARD_HOVER  0x1E2A42
#define COLOR_CARD_PRESS  0x253350
#define COLOR_TEXT         0xE8ECF4
#define COLOR_SUBTEXT     0x6B7A94
#define COLOR_ACCENT      0x00D4FF
#define COLOR_ACCENT_DIM  0x005A6B
#define COLOR_STATUSBAR   0x0D1220
#define COLOR_SUCCESS     0x00E676
#define COLOR_WARNING     0xFFB300
#define COLOR_DANGER      0xFF5252
#define COLOR_WIFI_ICON   0x4CAF50

/* ── Menu items ── */
typedef struct {
    const char *symbol;
    const char *label;
    uint32_t color;
} menu_item_t;

static const menu_item_t menu_items[] = {
    {LV_SYMBOL_IMAGE,    "Clock",    0x00D4FF},
    {LV_SYMBOL_AUDIO,    "Music",    0xFF6B35},
    {LV_SYMBOL_WIFI,     "WiFi",     0x4CAF50},
    {LV_SYMBOL_SETTINGS, "Setting",  0xFFD600},
    {LV_SYMBOL_LOOP,     "Weather",  0xE040FB},
    {LV_SYMBOL_LIST,     "More",     0x78909C},
};
#define MENU_COUNT 6

/* ── Globals ── */
static esp_lcd_touch_handle_t tp = NULL;
static bool wifi_connected = false;
static bool sntp_initialized = false;
static bool time_valid = false;
static lv_obj_t *clock_label = NULL;
static char saved_ssid[33] = {0};
static char saved_pass[65] = {0};

/* SH8601 init commands */
static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 10},
    {0x29, (uint8_t[]){0x00}, 0, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
};

/* Forward declarations */
static void create_main_screen(void);
static void create_wifi_screen(void);
static void clock_timer_cb(lv_timer_t *timer);
static void wifi_init_sta(const char *ssid, const char *pass);
static void sntp_init_and_sync(void);
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void wifi_scan_task(void *arg);
static void scan_btn_handler(lv_event_t *e);

/* ══════════════════════════════════════════
   LVGL porting
   ══════════════════════════════════════════ */

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_driver);
    return false;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
}

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t t = (esp_lcd_touch_handle_t)drv->user_data;
    uint16_t x, y;
    uint8_t cnt = 0;
    esp_lcd_touch_read_data(t);
    bool pressed = esp_lcd_touch_get_coordinates(t, &x, &y, NULL, &cnt, 1);
    if (pressed && cnt > 0) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void lvgl_increase_tick(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    while (1) {
        if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
            uint32_t delay = lv_timer_handler();
            xSemaphoreGive(lvgl_mux);
            if (delay > 500) delay = 500;
            if (delay < 1) delay = 1;
            vTaskDelay(pdMS_TO_TICKS(delay));
        }
    }
}

/* ══════════════════════════════════════════
   WiFi + SNTP
   ══════════════════════════════════════════ */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
    int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        time_valid = false;
        ESP_LOGI(TAG, "WiFi disconnected, retrying...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_connected = true;
        ESP_LOGI(TAG, "WiFi connected!");
        if (!sntp_initialized) {
            sntp_init_and_sync();
        }
    }
}

static bool netif_initialized = false;
static bool wifi_initialized = false;

static void wifi_init_sta(const char *ssid, const char *pass)
{
    /* Save credentials */
    if (ssid) strncpy(saved_ssid, ssid, sizeof(saved_ssid) - 1);
    if (pass) strncpy(saved_pass, pass, sizeof(saved_pass) - 1);

    if (!netif_initialized) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        netif_initialized = true;
    }

    if (!wifi_initialized) {
        esp_netif_create_default_wifi_sta();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
        wifi_initialized = true;
    }

    wifi_config_t wifi_config = {0};
    if (ssid && ssid[0]) {
        strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
        if (pass) strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi init done, connecting to %s...", ssid ? ssid : "(none)");
}

static void sntp_init_and_sync(void)
{
    if (sntp_initialized) return;
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();
    sntp_initialized = true;
    setenv("TZ", "CST-8", 1);
    tzset();

    /* Wait for time sync in background */
    xTaskCreatePinnedToCore((void (*)(void *))NULL, "sntp_wait", 2048, NULL, 1, NULL, 0);
}

/* ══════════════════════════════════════════
   Clock
   ══════════════════════════════════════════ */

static void clock_timer_cb(lv_timer_t *timer)
{
    lv_obj_t *label = (lv_obj_t *)timer->user_data;
    if (!label) return;

    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);

    /* Check if time is valid (year >= 2024) */
    if (ti.tm_year >= (2024 - 1900)) {
        time_valid = true;
        char buf[16];
        strftime(buf, sizeof(buf), "%H:%M", &ti);
        lv_label_set_text(label, buf);
    } else {
        /* Still waiting for SNTP */
        if (wifi_connected) {
            lv_label_set_text(label, "sync..");
        } else {
            lv_label_set_text(label, "--:--");
        }
    }
}

/* ══════════════════════════════════════════
   WiFi scan task
   ══════════════════════════════════════════ */

#define MAX_SCAN_RESULTS 15

typedef struct {
    char ssids[MAX_SCAN_RESULTS][33];
    int8_t rssi[MAX_SCAN_RESULTS];
    uint8_t authmode[MAX_SCAN_RESULTS];
    int count;
    lv_obj_t *list_obj;
} wifi_scan_data_t;

static wifi_scan_data_t scan_data = {0};

/* ── Password dialog ── */
static lv_obj_t *pwd_dialog = NULL;
static lv_obj_t *pwd_ta = NULL;
static lv_obj_t *pwd_kb = NULL;
static char selected_ssid[33] = {0};

static void pwd_connect_cb(lv_event_t *e);
static void pwd_cancel_cb(lv_event_t *e);

static void pwd_connect_cb(lv_event_t *e)
{
    const char *pass = lv_textarea_get_text(pwd_ta);
    ESP_LOGI(TAG, "Connecting to %s", selected_ssid);

    if (pwd_kb) { lv_obj_del(pwd_kb); pwd_kb = NULL; }
    if (pwd_dialog) { lv_obj_del(pwd_dialog); pwd_dialog = NULL; }

    /* Reconnect with new credentials */
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(300));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, selected_ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (pass && pass[0]) {
        strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);
    }
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_wifi_connect();

    /* Save */
    strncpy(saved_ssid, selected_ssid, sizeof(saved_ssid) - 1);
    if (pass) strncpy(saved_pass, pass, sizeof(saved_pass) - 1);

    /* Go back to main screen */
    create_main_screen();
}

static void pwd_cancel_cb(lv_event_t *e)
{
    if (pwd_kb) { lv_obj_del(pwd_kb); pwd_kb = NULL; }
    if (pwd_dialog) { lv_obj_del(pwd_dialog); pwd_dialog = NULL; }
}

static void show_password_dialog(const char *ssid)
{
    strncpy(selected_ssid, ssid, sizeof(selected_ssid) - 1);

    /* Dim overlay */
    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, LCD_H_RES, LCD_V_RES);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* Dialog */
    pwd_dialog = lv_obj_create(overlay);
    lv_obj_set_size(pwd_dialog, 320, 160);
    lv_obj_center(pwd_dialog);
    lv_obj_set_style_bg_color(pwd_dialog, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_radius(pwd_dialog, 16, 0);
    lv_obj_set_style_border_width(pwd_dialog, 0, 0);
    lv_obj_set_style_pad_all(pwd_dialog, 12, 0);
    lv_obj_clear_flag(pwd_dialog, LV_OBJ_FLAG_SCROLLABLE);

    /* Title */
    lv_obj_t *title = lv_label_create(pwd_dialog);
    char title_txt[64];
    snprintf(title_txt, sizeof(title_txt), LV_SYMBOL_WIFI " %s", ssid);
    lv_label_set_text(title, title_txt);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Password input */
    pwd_ta = lv_textarea_create(pwd_dialog);
    lv_textarea_set_one_line(pwd_ta, true);
    lv_textarea_set_password_mode(pwd_ta, true);
    lv_textarea_set_placeholder_text(pwd_ta, "Password...");
    lv_obj_set_size(pwd_ta, 296, 40);
    lv_obj_align(pwd_ta, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_bg_color(pwd_ta, lv_color_hex(0x1A2540), 0);
    lv_obj_set_style_text_color(pwd_ta, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_border_color(pwd_ta, lv_color_hex(COLOR_ACCENT), LV_STATE_FOCUSED);

    /* Buttons */
    lv_obj_t *btn_cancel = lv_btn_create(pwd_dialog);
    lv_obj_set_size(btn_cancel, 130, 36);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(COLOR_CARD_HOVER), 0);
    lv_obj_set_style_radius(btn_cancel, 10, 0);
    lv_obj_set_style_border_width(btn_cancel, 0, 0);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(btn_cancel, pwd_cancel_cb, LV_EVENT_CLICKED, overlay);
    lv_obj_t *cl = lv_label_create(btn_cancel);
    lv_label_set_text(cl, "Cancel");
    lv_obj_set_style_text_color(cl, lv_color_hex(COLOR_SUBTEXT), 0);
    lv_obj_center(cl);

    lv_obj_t *btn_ok = lv_btn_create(pwd_dialog);
    lv_obj_set_size(btn_ok, 130, 36);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_radius(btn_ok, 10, 0);
    lv_obj_set_style_border_width(btn_ok, 0, 0);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(btn_ok, pwd_connect_cb, LV_EVENT_CLICKED, overlay);
    lv_obj_t *ol = lv_label_create(btn_ok);
    lv_label_set_text(ol, "Connect");
    lv_obj_set_style_text_color(ol, lv_color_hex(COLOR_BG), 0);
    lv_obj_center(ol);

    /* Keyboard */
    pwd_kb = lv_keyboard_create(lv_layer_top());
    lv_keyboard_set_textarea(pwd_kb, pwd_ta);
    /* Move dialog up so keyboard doesn't cover it */
    lv_obj_align(pwd_dialog, LV_ALIGN_TOP_MID, 0, 20);
}

/* ── WiFi scan ── */
static void wifi_scan_task(void *arg)
{
    /* Initialize WiFi for scanning */
    static bool scan_netif_init = false;
    if (!scan_netif_init) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        scan_netif_init = true;
    }
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "Starting WiFi scan...");
    wifi_scan_config_t scan_conf = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };
    esp_wifi_scan_start(&scan_conf, true);

    uint16_t ap_count = MAX_SCAN_RESULTS;
    wifi_ap_record_t ap_records[MAX_SCAN_RESULTS];
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));

    scan_data.count = ap_count;
    for (int i = 0; i < ap_count && i < MAX_SCAN_RESULTS; i++) {
        strncpy(scan_data.ssids[i], (char *)ap_records[i].ssid, 32);
        scan_data.rssi[i] = ap_records[i].rssi;
        scan_data.authmode[i] = ap_records[i].authmode;
        ESP_LOGI(TAG, "  Found: %s (%d dBm)", ap_records[i].ssid, ap_records[i].rssi);
    }

    /* Deinit scan WiFi so we can reconnect properly later */
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    vTaskDelete(NULL);
}

/* ══════════════════════════════════════════
   UI: WiFi Screen
   ══════════════════════════════════════════ */

static void wifi_ssid_btn_cb(lv_event_t *e)
{
    const char *ssid = (const char *)lv_event_get_user_data(e);
    show_password_dialog(ssid);
}

static void wifi_back_cb(lv_event_t *e)
{
    create_main_screen();
}

static void create_wifi_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Top bar ── */
    lv_obj_t *topbar = lv_obj_create(scr);
    lv_obj_set_size(topbar, LCD_H_RES, 48);
    lv_obj_set_style_bg_color(topbar, lv_color_hex(COLOR_STATUSBAR), 0);
    lv_obj_set_style_radius(topbar, 0, 0);
    lv_obj_set_style_border_width(topbar, 0, 0);
    lv_obj_set_style_pad_all(topbar, 0, 0);
    lv_obj_clear_flag(topbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(topbar, LV_ALIGN_TOP_MID, 0, 0);

    /* Back button */
    lv_obj_t *back_btn = lv_btn_create(topbar);
    lv_obj_set_size(back_btn, 64, 34);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_set_style_pad_all(back_btn, 0, 0);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_add_event_cb(back_btn, wifi_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(back_lbl);

    /* Title */
    lv_obj_t *title = lv_label_create(topbar);
    lv_label_set_text(title, "WiFi Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_center(title);

    /* ── Connection status ── */
    lv_obj_t *status_card = lv_obj_create(scr);
    lv_obj_set_size(status_card, LCD_H_RES - 16, 48);
    lv_obj_set_style_bg_color(status_card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_radius(status_card, 12, 0);
    lv_obj_set_style_border_width(status_card, 0, 0);
    lv_obj_set_style_pad_all(status_card, 0, 0);
    lv_obj_clear_flag(status_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(status_card, LV_ALIGN_TOP_MID, 0, 56);

    lv_obj_t *status_icon = lv_label_create(status_card);
    lv_label_set_text(status_icon, wifi_connected ? LV_SYMBOL_WIFI : LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(status_icon, lv_color_hex(wifi_connected ? COLOR_SUCCESS : COLOR_SUBTEXT), 0);
    lv_obj_set_style_text_font(status_icon, &lv_font_montserrat_16, 0);
    lv_obj_align(status_icon, LV_ALIGN_LEFT_MID, 12, 0);

    lv_obj_t *status_txt = lv_label_create(status_card);
    if (wifi_connected && saved_ssid[0]) {
        char buf[80];
        snprintf(buf, sizeof(buf), "Connected\n%s", saved_ssid);
        lv_label_set_text(status_txt, buf);
    } else {
        lv_label_set_text(status_txt, "Not connected");
    }
    lv_obj_set_style_text_color(status_txt, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(status_txt, &lv_font_montserrat_14, 0);
    lv_obj_align(status_txt, LV_ALIGN_LEFT_MID, 44, 0);

    /* ── Scan button ── */
    lv_obj_t *scan_btn = lv_btn_create(scr);
    lv_obj_set_size(scan_btn, LCD_H_RES - 16, 40);
    lv_obj_set_style_bg_color(scan_btn, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_radius(scan_btn, 12, 0);
    lv_obj_set_style_border_width(scan_btn, 0, 0);
    lv_obj_set_style_shadow_width(scan_btn, 0, 0);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_MID, 0, 112);
    lv_obj_t *scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, LV_SYMBOL_WIFI " Scan Networks");
    lv_obj_set_style_text_color(scan_lbl, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_text_font(scan_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(scan_lbl);

    /* Scan click: run scan then rebuild WiFi screen */
    lv_obj_add_event_cb(scan_btn, scan_btn_handler, LV_EVENT_CLICKED, NULL);

    /* ── Network list ── */
    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, LCD_H_RES - 16, LCD_V_RES - 170);
    lv_obj_set_style_bg_color(list, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_radius(list, 12, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_ver(list, 8, 0);
    lv_obj_set_style_pad_hor(list, 4, 0);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 160);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC);

    if (scan_data.count > 0) {
        for (int i = 0; i < scan_data.count; i++) {
            char txt[80];
            snprintf(txt, sizeof(txt), LV_SYMBOL_WIFI " %s", scan_data.ssids[i]);

            lv_obj_t *btn = lv_list_add_btn(list, NULL, txt);
            lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(btn, lv_color_hex(COLOR_TEXT), 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_CARD), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_CARD_PRESS), LV_STATE_PRESSED);
            lv_obj_set_style_radius(btn, 8, 0);

            /* Signal strength text */
            lv_obj_t *sig = lv_label_create(btn);
            char sig_txt[24];
            snprintf(sig_txt, sizeof(sig_txt), "%ddBm %s",
                scan_data.rssi[i],
                scan_data.authmode[i] == WIFI_AUTH_OPEN ? "Open" : "***");
            lv_label_set_text(sig, sig_txt);
            lv_obj_set_style_text_color(sig, lv_color_hex(COLOR_SUBTEXT), 0);
            lv_obj_set_style_text_font(sig, &lv_font_montserrat_14, 0);
            lv_obj_align(sig, LV_ALIGN_RIGHT_MID, -4, 0);

            lv_obj_add_event_cb(btn, wifi_ssid_btn_cb, LV_EVENT_CLICKED, (void *)scan_data.ssids[i]);
        }
    } else {
        lv_obj_t *empty = lv_label_create(list);
        lv_label_set_text(empty, "No networks found.\nTap Scan to search.");
        lv_obj_set_style_text_color(empty, lv_color_hex(COLOR_SUBTEXT), 0);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_14, 0);
        lv_obj_set_style_pad_all(empty, 20, 0);
    }

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

/* Scan button handler */
static void scan_btn_handler(lv_event_t *e)
{
    /* Run scan in task */
    xTaskCreatePinnedToCore(wifi_scan_task, "wifi_scan", 4096, NULL, 1, NULL, 0);
    /* Wait then rebuild */
    vTaskDelay(pdMS_TO_TICKS(3000));
    create_wifi_screen();
}

/* ══════════════════════════════════════════
   UI: Main Screen
   ══════════════════════════════════════════ */

static void menu_btn_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Menu clicked: %s", menu_items[idx].label);

    switch (idx) {
        case 2: /* WiFi */
            /* Scan first, then show */
            xTaskCreatePinnedToCore(wifi_scan_task, "wifi_scan", 4096, NULL, 1, NULL, 0);
            vTaskDelay(pdMS_TO_TICKS(3000));
            create_wifi_screen();
            break;
        default:
            break;
    }
}

static void create_main_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Status bar ── */
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, LCD_H_RES, 44);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_STATUSBAR), 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);

    /* WiFi indicator (left) */
    lv_obj_t *wifi_icon = lv_label_create(bar);
    lv_label_set_text(wifi_icon, wifi_connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(wifi_icon,
        lv_color_hex(wifi_connected ? COLOR_WIFI_ICON : COLOR_SUBTEXT), 0);
    lv_obj_set_style_text_font(wifi_icon, &lv_font_montserrat_14, 0);
    lv_obj_align(wifi_icon, LV_ALIGN_LEFT_MID, 12, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "Dashboard");
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 36, 0);

    /* Clock (right) */
    clock_label = lv_label_create(bar);
    lv_label_set_text(clock_label, "--:--");
    lv_obj_set_style_text_color(clock_label, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(clock_label, &lv_font_montserrat_16, 0);
    lv_obj_align(clock_label, LV_ALIGN_RIGHT_MID, -14, 0);

    /* Clock timer */
    lv_timer_create(clock_timer_cb, 1000, clock_label);

    /* ── Menu grid ── */
    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_set_size(grid, LCD_H_RES - 20, LCD_V_RES - 70);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_gap(grid, 12, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    int btn_w = (LCD_H_RES - 20 - 24) / 3;
    int btn_h = 110;

    for (int i = 0; i < MENU_COUNT; i++) {
        lv_obj_t *btn = lv_btn_create(grid);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_CARD), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_CARD_PRESS), LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 18, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 12, 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_20, 0);
        lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        /* Accent line at top */
        lv_obj_t *accent = lv_obj_create(btn);
        lv_obj_set_size(accent, btn_w - 24, 3);
        lv_obj_set_style_bg_color(accent, lv_color_hex(menu_items[i].color), 0);
        lv_obj_set_style_radius(accent, 2, 0);
        lv_obj_set_style_border_width(accent, 0, 0);
        lv_obj_set_style_pad_all(accent, 0, 0);
        lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(accent, LV_ALIGN_TOP_MID, 0, 12);

        /* Icon */
        lv_obj_t *icon = lv_label_create(btn);
        lv_label_set_text(icon, menu_items[i].symbol);
        lv_obj_set_style_text_color(icon, lv_color_hex(menu_items[i].color), 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
        lv_obj_align(icon, LV_ALIGN_CENTER, 0, -4);

        /* Label */
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, menu_items[i].label);
        lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_SUBTEXT), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -12);

        lv_obj_add_event_cb(btn, menu_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    /* ── Bottom bar ── */
    lv_obj_t *bottom = lv_obj_create(scr);
    lv_obj_set_size(bottom, LCD_H_RES, 24);
    lv_obj_set_style_bg_opa(bottom, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottom, 0, 0);
    lv_obj_set_style_pad_all(bottom, 0, 0);
    lv_obj_clear_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *ind = lv_obj_create(bottom);
    lv_obj_set_size(ind, 80, 4);
    lv_obj_set_style_bg_color(ind, lv_color_hex(COLOR_ACCENT_DIM), 0);
    lv_obj_set_style_radius(ind, 2, 0);
    lv_obj_set_style_border_width(ind, 0, 0);
    lv_obj_set_style_pad_all(ind, 0, 0);
    lv_obj_clear_flag(ind, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(ind);

    /* Battery icon */
    lv_obj_t *bat = lv_label_create(bottom);
    lv_label_set_text(bat, LV_SYMBOL_BATTERY_3);
    lv_obj_set_style_text_color(bat, lv_color_hex(COLOR_SUBTEXT), 0);
    lv_obj_set_style_text_font(bat, &lv_font_montserrat_14, 0);
    lv_obj_align(bat, LV_ALIGN_RIGHT_MID, -12, 0);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
}

/* ══════════════════════════════════════════
   Main entry
   ══════════════════════════════════════════ */

void app_main(void)
{
    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    esp_log_level_set("FT5x06", ESP_LOG_NONE);

    static lv_disp_draw_buf_t disp_buf;
    static lv_disp_drv_t disp_drv;

    /* NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* I2C */
    ESP_LOGI(TAG, "Initialize I2C bus");
    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_TOUCH_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = PIN_TOUCH_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 200 * 1000,
    };
    ESP_ERROR_CHECK(i2c_param_config(TOUCH_HOST, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(TOUCH_HOST, i2c_conf.mode, 0, 0, 0));

    /* TCA9554 IO expander */
    esp_io_expander_handle_t io_expander = NULL;
    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(
        TOUCH_HOST, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander));
    esp_io_expander_set_dir(io_expander,
        IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2,
        IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander,
        IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_io_expander_set_level(io_expander,
        IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2, 1);

    /* SPI bus */
    ESP_LOGI(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        PIN_LCD_PCLK, PIN_LCD_DATA0, PIN_LCD_DATA1, PIN_LCD_DATA2, PIN_LCD_DATA3,
        LCD_H_RES * LCD_V_RES * LCD_BIT_PER_PIXEL / 8);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* LCD Panel IO */
    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config =
        SH8601_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, notify_lvgl_flush_ready, &disp_drv);
    sh8601_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = { .use_qspi_interface = 1 },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    /* SH8601 panel */
    esp_lcd_panel_handle_t panel_handle = NULL;
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    /* Touch */
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_config =
        ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(
        (esp_lcd_i2c_bus_handle_t)TOUCH_HOST, &tp_io_config, &tp_io_handle));
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = PIN_TOUCH_RST,
        .int_gpio_num = PIN_TOUCH_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp));

    /* LVGL init */
    ESP_LOGI(TAG, "Initialize LVGL");
    lv_init();
    lv_color_t *buf1 = heap_caps_malloc(
        LCD_H_RES * LVGL_BUF_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_DMA);
    lv_color_t *buf2 = heap_caps_malloc(
        LCD_H_RES * LVGL_BUF_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1 && buf2);
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, LCD_H_RES * LVGL_BUF_HEIGHT);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    /* LVGL tick */
    const esp_timer_create_args_t tick_args = {
        .callback = &lvgl_increase_tick,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    /* Touch input */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = lvgl_touch_cb;
    indev_drv.user_data = tp;
    lv_indev_drv_register(&indev_drv);

    /* LVGL task */
    lvgl_mux = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(lvgl_port_task, "LVGL",
        LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL, 1);

    /* Create UI */
    if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
        create_main_screen();
        xSemaphoreGive(lvgl_mux);
    }

    /* Start WiFi (connect to saved or scan) */
    /* Default: try to connect. User can configure via WiFi screen */
    ESP_LOGI(TAG, "System ready! Use WiFi menu to configure network.");
}
