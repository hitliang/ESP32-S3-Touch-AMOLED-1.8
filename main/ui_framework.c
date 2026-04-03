/*
 * ui_framework.c - UI 框架实现：状态栏 + 3x2 应用图标
 */

#include "ui_framework.h"
#include "display.h"
#include "esp_log.h"

static const char *TAG = "ui_framework";

static ui_framework_t g_ui = {0};

// 默认应用名称
static const char *default_app_names[] = {
    "WiFi",
    "蓝牙",
    "音乐",
    "传感器",
    "时钟",
    "设置"
};

// WiFi 信号图标 (简单的 ASCII art 风格)
static const char *wifi_icons[] = {
    "WiFi\n✕",    // 无信号
    "WiFi\n▂",    // 弱
    "WiFi\n▂▄",   // 中
    "WiFi\n▂▄▆",  // 强
    "WiFi\n▂▄▆█"  // 满格
};

// 电池图标
static const char *battery_icons[] = {
    "🪫",  // 0-20%
    "🔋",  // 21-50%
    "🔋",  // 51-80%
    "🔋⚡"  // 81-100%
};

// ==================== 内部函数 ====================

static int get_wifi_level(int rssi)
{
    if (rssi >= -50) return 4;      // 满格
    else if (rssi >= -60) return 3;  // 强
    else if (rssi >= -70) return 2;  // 中
    else if (rssi >= -80) return 1;  // 弱
    else return 0;                   // 无信号
}

static int get_battery_level(uint8_t percent)
{
    if (percent >= 80) return 3;
    else if (percent >= 50) return 2;
    else if (percent >= 20) return 1;
    else return 0;
}

static lv_color_t get_wifi_color(int level)
{
    switch (level) {
        case 4:
        case 3: return UI_COLOR_WIFI_HIGH;
        case 2: return UI_COLOR_WIFI_MID;
        default: return UI_COLOR_WIFI_LOW;
    }
}

static lv_color_t get_battery_color(uint8_t percent)
{
    if (percent >= 80) return UI_COLOR_BAT_HIGH;
    else if (percent >= 50) return UI_COLOR_BAT_MID;
    else return UI_COLOR_BAT_LOW;
}

// 应用点击回调
static void app_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    ui_app_id_t app_id = (ui_app_id_t)(uintptr_t)lv_obj_get_user_data(btn);
    
    ESP_LOGI(TAG, "App clicked: %d", app_id);
    
    if (g_ui.callbacks[app_id]) {
        g_ui.callbacks[app_id]();
    }
}

// 创建状态栏
static lv_obj_t* create_status_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, UI_LCD_WIDTH, UI_STATUS_BAR_HEIGHT);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, UI_STATUS_BAR_BG_COLOR, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    
    // 布局：左侧 WiFi，中间时间，右侧电池
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(bar, 10, 0);
    lv_obj_set_style_pad_right(bar, 10, 0);
    
    // WiFi 标签
    g_ui.wifi_label = lv_label_create(bar);
    lv_label_set_text(g_ui.wifi_label, "WiFi ▂▄▆");
    lv_obj_set_style_text_color(g_ui.wifi_label, UI_COLOR_WIFI_HIGH, 0);
    lv_obj_set_style_text_font(g_ui.wifi_label, &lv_font_montserrat_14, 0);
    
    // 时间标签
    g_ui.time_label = lv_label_create(bar);
    lv_label_set_text(g_ui.time_label, "12:00");
    lv_obj_set_style_text_color(g_ui.time_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(g_ui.time_label, &lv_font_montserrat_16, 0);
    
    // 电池标签
    g_ui.battery_label = lv_label_create(bar);
    lv_label_set_text(g_ui.battery_label, "🔋 85%");
    lv_obj_set_style_text_color(g_ui.battery_label, UI_COLOR_BAT_HIGH, 0);
    lv_obj_set_style_text_font(g_ui.battery_label, &lv_font_montserrat_14, 0);
    
    return bar;
}

// 创建应用图标
static lv_obj_t* create_app_icon(lv_obj_t *parent, ui_app_id_t app_id, int row, int col)
{
    // 计算位置
    int start_x = (UI_LCD_WIDTH - (UI_APP_ICON_SIZE * UI_APP_GRID_COLS + UI_APP_ICON_GAP * (UI_APP_GRID_COLS - 1))) / 2;
    int start_y = UI_STATUS_BAR_HEIGHT + 20;  // 状态栏下方 20px 留白
    
    int x = start_x + col * (UI_APP_ICON_SIZE + UI_APP_ICON_GAP);
    int y = start_y + row * (UI_APP_ICON_SIZE + UI_APP_ICON_GAP + UI_APP_LABEL_HEIGHT);
    
    // 创建按钮容器
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, UI_APP_ICON_SIZE, UI_APP_ICON_SIZE + UI_APP_LABEL_HEIGHT);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_70, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 5, 0);
    lv_obj_set_user_data(btn, (void*)(uintptr_t)app_id);
    
    // 添加点击事件
    lv_obj_add_event_cb(btn, app_click_cb, LV_EVENT_CLICKED, NULL);
    
    // 垂直布局
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // 图标区域（使用简单的彩色方块作为占位符）
    lv_obj_t *icon_bg = lv_obj_create(btn);
    lv_obj_set_size(icon_bg, 50, 50);
    lv_obj_set_style_bg_color(icon_bg, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_radius(icon_bg, 10, 0);
    lv_obj_set_style_border_width(icon_bg, 0, 0);
    lv_obj_set_style_shadow_width(icon_bg, 0, 0);
    lv_obj_set_style_pad_all(icon_bg, 0, 0);
    
    // 图标内的文字（首字母）
    lv_obj_t *icon_char = lv_label_create(icon_bg);
    char first_char = default_app_names[app_id][0];
    char char_str[2] = {first_char, '\0'};
    lv_label_set_text(icon_char, char_str);
    lv_obj_set_style_text_color(icon_char, lv_color_white(), 0);
    lv_obj_set_style_text_font(icon_char, &lv_font_montserrat_24, 0);
    lv_obj_center(icon_char);
    
    // 应用名称标签
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, default_app_names[app_id]);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_top(label, 5, 0);
    
    // 保存引用
    g_ui.app_icons[app_id] = btn;
    g_ui.app_labels[app_id] = label;
    
    return btn;
}

// 创建应用网格
static lv_obj_t* create_app_grid(lv_obj_t *parent)
{
    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_set_size(grid, UI_LCD_WIDTH, UI_LCD_HEIGHT - UI_STATUS_BAR_HEIGHT);
    lv_obj_set_pos(grid, 0, UI_STATUS_BAR_HEIGHT);
    lv_obj_set_style_bg_color(grid, UI_COLOR_BG, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_radius(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    
    // 创建 3x2 应用图标
    for (int row = 0; row < UI_APP_GRID_ROWS; row++) {
        for (int col = 0; col < UI_APP_GRID_COLS; col++) {
            ui_app_id_t app_id = (ui_app_id_t)(row * UI_APP_GRID_COLS + col);
            if (app_id < UI_APP_MAX) {
                create_app_icon(grid, app_id, row, col);
            }
        }
    }
    
    return grid;
}

// ==================== 公共 API ====================

esp_err_t ui_framework_init(void)
{
    ESP_LOGI(TAG, "Initializing UI framework...");
    
    // 获取屏幕对象
    lv_obj_t *scr = lv_scr_act();
    if (!scr) {
        ESP_LOGE(TAG, "Failed to get screen object");
        return ESP_FAIL;
    }
    
    // 清空屏幕
    lv_obj_clean(scr);
    
    // 设置背景色
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG, 0);
    
    // 创建状态栏
    g_ui.status_bar = create_status_bar(scr);
    
    // 创建应用网格
    g_ui.app_grid = create_app_grid(scr);
    
    // 初始化回调数组
    for (int i = 0; i < UI_APP_MAX; i++) {
        g_ui.callbacks[i] = NULL;
    }
    
    // 设置默认值
    ui_update_wifi_signal(-65);  // 中等信号
    ui_update_battery(85);       // 85% 电量
    ui_update_time(12, 0);       // 12:00
    
    ESP_LOGI(TAG, "UI framework initialized");
    return ESP_OK;
}

ui_framework_t* ui_framework_get_handle(void)
{
    return &g_ui;
}

void ui_update_wifi_signal(int rssi)
{
    if (!g_ui.wifi_label) return;
    
    int level = get_wifi_level(rssi);
    lv_label_set_text(g_ui.wifi_label, wifi_icons[level]);
    lv_obj_set_style_text_color(g_ui.wifi_label, get_wifi_color(level), 0);
}

void ui_update_battery(uint8_t percent)
{
    if (!g_ui.battery_label) return;
    
    int level = get_battery_level(percent);
    char buf[16];
    snprintf(buf, sizeof(buf), "%s %d%%", battery_icons[level], percent);
    lv_label_set_text(g_ui.battery_label, buf);
    lv_obj_set_style_text_color(g_ui.battery_label, get_battery_color(percent), 0);
}

void ui_update_time(uint8_t hour, uint8_t minute)
{
    if (!g_ui.time_label) return;
    
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
    lv_label_set_text(g_ui.time_label, buf);
}

void ui_register_app_callback(ui_app_id_t app_id, ui_app_callback_t callback)
{
    if (app_id < UI_APP_MAX) {
        g_ui.callbacks[app_id] = callback;
        ESP_LOGI(TAG, "Registered callback for app %d", app_id);
    }
}

void ui_set_app_name(ui_app_id_t app_id, const char *name)
{
    if (app_id < UI_APP_MAX && g_ui.app_labels[app_id] && name) {
        lv_label_set_text(g_ui.app_labels[app_id], name);
    }
}

void ui_set_app_icon(ui_app_id_t app_id, const void *icon_data)
{
    // 预留自定义图标支持
    // 目前使用默认彩色方块 + 首字母
    (void)app_id;
    (void)icon_data;
}
