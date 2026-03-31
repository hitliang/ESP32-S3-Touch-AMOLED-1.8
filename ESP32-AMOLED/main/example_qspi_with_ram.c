/*
 * main.c - ESP32-S3-Touch-AMOLED-1.8
 * Visual upgrade + WiFi settings + SNTP clock (LVGL 8)
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
#include "nvs_flash.h"
#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_io_expander_tca9554.h"
#include "lvgl.h"

static const char *TAG = "main";
static SemaphoreHandle_t lvgl_mux = NULL;

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

/* Colors */
#define COLOR_BG         0x0D1B2A
#define COLOR_CARD       0x1B2838
#define COLOR_CARD_PRESS 0x2A3F55
#define COLOR_TEXT       0xFFFFFF
#define COLOR_SUBTEXT    0x8899AA
#define COLOR_ACCENT     0x00E5FF
#define COLOR_STATUSBAR  0x111D2C

/* Fake SSIDs for demo */
static const char *fake_ssids[] = {"Home_WiFi", "Office_5G", "Coffee_Shop", "IoT_Device"};
#define FAKE_SSID_COUNT 4

/* Menu items */
typedef struct {
    const char *symbol;
    const char *label;
    lv_color_t color;
} menu_item_t;

static const menu_item_t menu_items[] = {
    {LV_SYMBOL_IMAGE,    "Clock",   LV_COLOR_MAKE(0x00, 0xE5, 0xFF)},
    {LV_SYMBOL_AUDIO,    "Music",   LV_COLOR_MAKE(0xFF, 0x6B, 0x35)},
    {LV_SYMBOL_WIFI,     "WiFi",    LV_COLOR_MAKE(0x00, 0xE6, 0x76)},
    {LV_SYMBOL_SETTINGS, "Setting", LV_COLOR_MAKE(0xFF, 0xD6, 0x00)},
    {LV_SYMBOL_LOOP,     "Weather", LV_COLOR_MAKE(0xE0, 0x40, 0xFB)},
    {LV_SYMBOL_LIST,     "More",    LV_COLOR_MAKE(0x78, 0x90, 0x9C)},
};
#define MENU_COUNT 6

/* Globals */
static esp_lcd_touch_handle_t tp = NULL;
static bool wifi_connected = false;
static bool sntp_initialized = false;
static lv_obj_t *clock_label = NULL;

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
static void create_menu_ui(void);
static void create_wifi_screen(void);
static void wifi_btn_event_cb(lv_event_t *e);
static void back_btn_event_cb(lv_event_t *e);
static void ssid_btn_event_cb(lv_event_t *e);
static void wifi_switch_event_cb(lv_event_t *e);
static void password_close_cb(lv_event_t *e);
static void clock_timer_cb(lv_timer_t *timer);
static void wifi_init_sta(void);
static void sntp_init_and_sync(void);
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

/* ===== LVGL callbacks ===== */

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

/* ===== WiFi & SNTP ===== */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
    int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        ESP_LOGI(TAG, "WiFi disconnected, retrying...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_connected = true;
        ESP_LOGI(TAG, "WiFi connected!");
        if (!sntp_initialized) {
            sntp_init_and_sync();
        }
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "YOUR_SSID",
            .password = "YOUR_PASSWORD",
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi init done, connecting...");
}

static void sntp_init_and_sync(void)
{
    if (sntp_initialized) return;
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    sntp_initialized = true;
    setenv("TZ", "CST-8", 1);
    tzset();
}

/* ===== Clock timer (1s) ===== */

static void clock_timer_cb(lv_timer_t *timer)
{
    lv_obj_t *label = (lv_obj_t *)timer->user_data;
    if (!label) return;

    if (wifi_connected) {
        time_t now;
        struct tm ti;
        time(&now);
        localtime_r(&now, &ti);
        char buf[8];
        strftime(buf, sizeof(buf), "%H:%M", &ti);
        lv_label_set_text(label, buf);
    } else {
        lv_label_set_text(label, "--:--");
    }
}

/* ===== Password dialog ===== */

static void password_close_cb(lv_event_t *e)
{
    lv_obj_t *mbox = lv_event_get_user_data(e);
    lv_obj_del(mbox);
}

static void show_password_dialog(const char *ssid)
{
    lv_obj_t *mbox = lv_msgbox_create(NULL, "WiFi Password", "Enter password:", NULL, true);
    lv_obj_set_size(mbox, 300, 320);
    lv_obj_center(mbox);

    lv_obj_t *ta = lv_textarea_create(mbox);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, true);
    lv_textarea_set_placeholder_text(ta, "Password...");
    lv_obj_set_width(ta, 260);

    lv_obj_t *kb = lv_keyboard_create(lv_layer_top());
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_add_event_cb(mbox, password_close_cb, LV_EVENT_DELETE, NULL);
}

/* ===== WiFi screen ===== */

static void create_wifi_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), 0);

    /* Top bar */
    lv_obj_t *topbar = lv_obj_create(scr);
    lv_obj_set_size(topbar, LCD_H_RES, 44);
    lv_obj_set_style_bg_color(topbar, lv_color_hex(COLOR_STATUSBAR), 0);
    lv_obj_set_style_radius(topbar, 0, 0);
    lv_obj_set_style_border_width(topbar, 0, 0);
    lv_obj_set_style_pad_all(topbar, 0, 0);
    lv_obj_clear_flag(topbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(topbar, LV_ALIGN_TOP_MID, 0, 0);

    /* Back button */
    lv_obj_t *back_btn = lv_btn_create(topbar);
    lv_obj_set_size(back_btn, 70, 32);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);

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

    /* WiFi switch row */
    lv_obj_t *sw_row = lv_obj_create(scr);
    lv_obj_set_size(sw_row, LCD_H_RES - 16, 44);
    lv_obj_set_style_bg_color(sw_row, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_radius(sw_row, 12, 0);
    lv_obj_set_style_border_width(sw_row, 0, 0);
    lv_obj_set_style_pad_all(sw_row, 0, 0);
    lv_obj_clear_flag(sw_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(sw_row, LV_ALIGN_TOP_MID, 0, 52);

    lv_obj_t *sw_lbl = lv_label_create(sw_row);
    lv_label_set_text(sw_lbl, LV_SYMBOL_WIFI " WiFi");
    lv_obj_set_style_text_color(sw_lbl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(sw_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(sw_lbl, LV_ALIGN_LEFT_MID, 12, 0);

    lv_obj_t *sw = lv_switch_create(sw_row);
    lv_obj_set_size(sw, 48, 26);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, wifi_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* SSID list */
    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, LCD_H_RES - 16, LCD_V_RES - 120);
    lv_obj_set_style_bg_color(list, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_radius(list, 12, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 8, 0);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 104);

    for (int i = 0; i < FAKE_SSID_COUNT; i++) {
        char txt[64];
        snprintf(txt, sizeof(txt), LV_SYMBOL_WIFI " %s", fake_ssids[i]);

        lv_obj_t *btn = lv_list_add_btn(list, NULL, txt);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_CARD), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_CARD_PRESS), LV_STATE_PRESSED);

        /* Signal icon */
        lv_obj_t *sig = lv_label_create(btn);
        const char *icons[] = {
            LV_SYMBOL_BATTERY_EMPTY,
            LV_SYMBOL_BATTERY_1,
            LV_SYMBOL_BATTERY_2,
            LV_SYMBOL_BATTERY_3,
            LV_SYMBOL_CLOSE
        };
        lv_label_set_text(sig, icons[i % 5]);
        lv_obj_set_style_text_color(sig, lv_color_hex(COLOR_SUBTEXT), 0);
        lv_obj_align(sig, LV_ALIGN_RIGHT_MID, 0, 0);

        lv_obj_add_event_cb(btn, ssid_btn_event_cb, LV_EVENT_CLICKED, (void *)fake_ssids[i]);
    }

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

/* ===== Event callbacks ===== */

static void wifi_btn_event_cb(lv_event_t *e)
{
    create_wifi_screen();
}

static void back_btn_event_cb(lv_event_t *e)
{
    /* Recreate main screen */
    lv_obj_t *old = lv_scr_act();
    lv_disp_t *d = lv_disp_get_default();
    lv_obj_t *new_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(new_scr, lv_color_hex(COLOR_BG), 0);
    lv_disp_load_scr(new_scr);
    create_menu_ui();
    (void)old;
}

static void ssid_btn_event_cb(lv_event_t *e)
{
    const char *ssid = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Selected SSID: %s", ssid);
    show_password_dialog(ssid);
}

static void wifi_switch_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
        ESP_LOGI(TAG, "WiFi ON");
    } else {
        ESP_LOGI(TAG, "WiFi OFF");
    }
}

/* ===== Main Menu UI ===== */

static void create_menu_ui(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), 0);

    /* Status bar */
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, LCD_H_RES, 40);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_STATUSBAR), 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, LV_SYMBOL_HOME " Dashboard");
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 12, 0);

    /* Clock */
    clock_label = lv_label_create(bar);
    lv_label_set_text(clock_label, "--:--");
    lv_obj_set_style_text_color(clock_label, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(clock_label, &lv_font_montserrat_16, 0);
    lv_obj_align(clock_label, LV_ALIGN_RIGHT_MID, -12, 0);

    /* Clock timer */
    lv_timer_create(clock_timer_cb, 1000, clock_label);

    /* Grid */
    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_set_size(grid, LCD_H_RES - 24, LCD_V_RES - 64);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_gap(grid, 14, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    int btn_w = (LCD_H_RES - 24 - 28) / 3;
    int btn_h = 100;

    for (int i = 0; i < MENU_COUNT; i++) {
        lv_obj_t *btn = lv_btn_create(grid);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_CARD), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_CARD_PRESS), LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 16, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 10, 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_20, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        /* Icon */
        lv_obj_t *icon = lv_label_create(btn);
        lv_label_set_text(icon, menu_items[i].symbol);
        lv_obj_set_style_text_color(icon, menu_items[i].color, 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 14);

        /* Label */
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, menu_items[i].label);
        lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_SUBTEXT), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -10);

        /* WiFi button event */
        if (i == 2) {
            lv_obj_add_event_cb(btn, wifi_btn_event_cb, LV_EVENT_CLICKED, NULL);
        }
    }

    /* Bottom indicator */
    lv_obj_t *ind = lv_obj_create(scr);
    lv_obj_set_size(ind, 60, 4);
    lv_obj_set_style_bg_color(ind, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_radius(ind, 2, 0);
    lv_obj_set_style_border_width(ind, 0, 0);
    lv_obj_set_style_pad_all(ind, 0, 0);
    lv_obj_clear_flag(ind, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(ind, LV_ALIGN_BOTTOM_MID, 0, -8);
}

/* ===== Main ===== */

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

    /* TCA9554 */
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
        create_menu_ui();
        xSemaphoreGive(lvgl_mux);
    }

    /* Start WiFi */
    wifi_init_sta();

    ESP_LOGI(TAG, "System ready!");
}
