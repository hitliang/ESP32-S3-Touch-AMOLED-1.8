/*
 * ui.c - LVGL 9 用户界面 - 现代化主页
 */

#include "ui.h"
#include "display.h"

#include "freertos/FreeRTOS.h"
#include "esp_log.h"

static const char *TAG = "ui";

// ==================== 颜色定义 ====================
#define COLOR_BG        lv_color_hex(0x0A0A14)
#define COLOR_CARD      lv_color_hex(0x1A1A2E)
#define COLOR_PRIMARY   lv_color_hex(0x00D4FF)
#define COLOR_ACCENT    lv_color_hex(0xFF6B35)
#define COLOR_GREEN     lv_color_hex(0x00E676)
#define COLOR_TEXT      lv_color_hex(0xFFFFFF)
#define COLOR_SUBTEXT   lv_color_hex(0x8888AA)

// 屏幕尺寸（从 HARDWARE.md）
#define LCD_W  368
#define LCD_H  448

// ==================== 静态对象 ====================
static lv_obj_t *time_label = NULL;
static lv_obj_t *wifi_label = NULL;

// ==================== 工具函数 ====================

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, lv_color_t color, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_font(l, font, 0);
    return l;
}

// ==================== 信息卡片 ====================

static lv_obj_t *info_tile(lv_obj_t *parent, const char *title, const char *value,
                            const char *unit, lv_color_t bar_color)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(100), 80);
    lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 12, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // 顶部彩色条
    lv_obj_t *bar = lv_obj_create(card);
    lv_obj_set_size(bar, LV_PCT(100), 3);
    lv_obj_set_style_bg_color(bar, bar_color, 0);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);

    // 标题
    make_label(card, title, COLOR_SUBTEXT, &lv_font_montserrat_12);
    lv_obj_align(lv_obj_get_child(card, -1), LV_ALIGN_TOP_LEFT, 12, 10);

    // 数值
    lv_obj_t *v = make_label(card, value, COLOR_TEXT, &lv_font_montserrat_24);
    lv_obj_align(v, LV_ALIGN_TOP_LEFT, 12, 30);

    // 单位
    if (unit) {
        lv_obj_t *u = make_label(card, unit, COLOR_SUBTEXT, &lv_font_montserrat_12);
        lv_obj_align_to(u, v, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -2);
    }

    return card;
}

// ==================== 快捷按钮 ====================

static void shortcut_cb(lv_event_t *e)
{
    const char *name = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Clicked: %s", name);
}

static lv_obj_t *shortcut_btn(lv_obj_t *parent, const char *emoji, const char *name, lv_color_t color)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LV_PCT(100), 60);
    lv_obj_set_style_bg_color(btn, COLOR_CARD, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 6, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_10, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(btn, 12, 0);
    lv_obj_set_style_pad_gap(btn, 10, 0);
    lv_obj_add_event_cb(btn, shortcut_cb, LV_EVENT_CLICKED, (void *)name);

    // Emoji 图标
    make_label(btn, emoji, color, &lv_font_montserrat_20);

    // 名称
    make_label(btn, name, COLOR_TEXT, &lv_font_montserrat_14);

    // 右箭头
    lv_obj_t *arrow = make_label(btn, "\xEF\x81\xA1", COLOR_SUBTEXT, &lv_font_montserrat_14);
    lv_obj_set_style_margin_left(arrow, LV_PCT(100), 0);  // push right
    // Use flex grow on spacer
    lv_obj_t *spacer = lv_obj_create(btn);
    lv_obj_set_size(spacer, 0, 0);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_flex_grow(spacer, 1);

    return btn;
}

// ==================== 状态栏 ====================

static void create_status_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), 32);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0D0D1A), 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(bar, 14, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    // 时间
    time_label = make_label(bar, "--:--", COLOR_TEXT, &lv_font_montserrat_14);

    // 标题
    make_label(bar, "ESP32-AMOLED", COLOR_PRIMARY, &lv_font_montserrat_14);

    // 右侧图标
    lv_obj_t *right = lv_obj_create(bar);
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_pad_all(right, 0, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(right, 6, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    wifi_label = make_label(right, "\xEF\x87\xAB", COLOR_SUBTEXT, &lv_font_montserrat_14);
    make_label(right, "\xEF\x89\x80", COLOR_GREEN, &lv_font_montserrat_14);
}

// ==================== 主界面 ====================

void ui_create(void)
{
    ESP_LOGI(TAG, "Creating UI...");

    // 全屏暗色背景
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);

    // 主布局容器
    lv_obj_t *root = lv_obj_create(scr);
    lv_obj_set_size(root, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(root, COLOR_BG, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_pad_gap(root, 0, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(root, 10, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    // ===== 状态栏 =====
    create_status_bar(root);

    // ===== 欢迎 =====
    lv_obj_t *welcome = lv_obj_create(root);
    lv_obj_set_size(welcome, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(welcome, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(welcome, 0, 0);
    lv_obj_set_style_pad_top(welcome, 6, 0);
    lv_obj_set_style_pad_bottom(welcome, 4, 0);
    lv_obj_clear_flag(welcome, LV_OBJ_FLAG_SCROLLABLE);

    make_label(welcome, "Hello, ESP32-S3!", COLOR_TEXT, &lv_font_montserrat_22);
    lv_obj_align(lv_obj_get_child(welcome, -1), LV_ALIGN_TOP_LEFT, 0, 0);
    make_label(welcome, "Touch AMOLED 1.8  |  Vibe Coding", COLOR_SUBTEXT, &lv_font_montserrat_12);
    lv_obj_align(lv_obj_get_child(welcome, -1), LV_ALIGN_TOP_LEFT, 0, 30);

    // ===== 信息卡片 (2x2) =====
    lv_obj_t *grid = lv_obj_create(root);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_gap(grid, 8, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    // 每个卡片占 50% 宽度
    lv_obj_t *t1 = info_tile(grid, "CPU", "0", "%", COLOR_PRIMARY);
    lv_obj_set_width(t1, LV_PCT(48));
    lv_obj_t *t2 = info_tile(grid, "RAM", "0", "MB", COLOR_ACCENT);
    lv_obj_set_width(t2, LV_PCT(48));
    lv_obj_t *t3 = info_tile(grid, "TEMP", "--", "\xC2\xB0" "C", COLOR_GREEN);
    lv_obj_set_width(t3, LV_PCT(48));
    lv_obj_t *t4 = info_tile(grid, "BATT", "--", "%", COLOR_PRIMARY);
    lv_obj_set_width(t4, LV_PCT(48));
    (void)t1; (void)t2; (void)t3; (void)t4;  // suppress unused warning

    // ===== 快捷功能 =====
    make_label(root, "快捷功能", COLOR_SUBTEXT, &lv_font_montserrat_12);
    lv_obj_set_style_pad_top(lv_obj_get_child(root, -1), 6, 0);
    lv_obj_set_style_pad_bottom(lv_obj_get_child(root, -1), 4, 0);

    lv_obj_t *list = lv_obj_create(root);
    lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_gap(list, 6, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    shortcut_btn(list, "\xEF\x84\xA2", "Wi-Fi 网络", COLOR_PRIMARY);
    shortcut_btn(list, "\xEF\x80\x81", "音乐播放", COLOR_ACCENT);
    shortcut_btn(list, "\xEF\x80\x97", "实时时钟", COLOR_GREEN);
    shortcut_btn(list, "\xEF\x80\x93", "系统设置", COLOR_SUBTEXT);

    ESP_LOGI(TAG, "UI created");
}

// ==================== 更新接口 ====================

void ui_update_time(const char *time_str)
{
    if (time_label && display_lock(100)) {
        lv_label_set_text(time_label, time_str);
        display_unlock();
    }
}

void ui_update_wifi_status(bool connected, const char *ssid)
{
    if (wifi_label && display_lock(100)) {
        lv_obj_set_style_text_color(wifi_label, connected ? COLOR_GREEN : COLOR_SUBTEXT, 0);
        display_unlock();
    }
}
