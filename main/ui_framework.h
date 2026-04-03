/*
 * ui_framework.h - UI 框架：状态栏 + 3x2 应用图标
 */

#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 屏幕参数 ====================
#define UI_LCD_WIDTH    368
#define UI_LCD_HEIGHT   448

// ==================== 状态栏参数 ====================
#define UI_STATUS_BAR_HEIGHT  40
#define UI_STATUS_BAR_BG_COLOR  lv_color_hex(0x1a1a1a)  // 深灰色背景

// ==================== 应用图标参数 ====================
#define UI_APP_GRID_ROWS     3
#define UI_APP_GRID_COLS     2
#define UI_APP_ICON_SIZE     100  // 图标大小
#define UI_APP_ICON_GAP      10   // 图标间距
#define UI_APP_LABEL_HEIGHT  20   // 标签高度

// ==================== 颜色主题 ====================
#define UI_COLOR_BG         lv_color_hex(0x000000)  // 黑色背景
#define UI_COLOR_ACCENT     lv_color_hex(0x00a8ff)  // 蓝色强调色
#define UI_COLOR_TEXT       lv_color_hex(0xffffff)  // 白色文字
#define UI_COLOR_WIFI_HIGH  lv_color_hex(0x00ff00)  // WiFi 信号强 - 绿色
#define UI_COLOR_WIFI_MID   lv_color_hex(0xffff00)  // WiFi 信号中 - 黄色
#define UI_COLOR_WIFI_LOW   lv_color_hex(0xff0000)  // WiFi 信号弱 - 红色
#define UI_COLOR_BAT_HIGH   lv_color_hex(0x00ff00)  // 电量高 - 绿色
#define UI_COLOR_BAT_MID    lv_color_hex(0xffff00)  // 电量中 - 黄色
#define UI_COLOR_BAT_LOW    lv_color_hex(0xff0000)  // 电量低 - 红色

// ==================== 应用图标定义 ====================
typedef enum {
    UI_APP_WIFI = 0,      // WiFi 设置
    UI_APP_BLUETOOTH,     // 蓝牙
    UI_APP_MUSIC,         // 音乐播放器
    UI_APP_SENSOR,        // 传感器
    UI_APP_CLOCK,         // 时钟
    UI_APP_SETTINGS,      // 设置
    UI_APP_MAX
} ui_app_id_t;

// ==================== 应用回调函数 ====================
typedef void (*ui_app_callback_t)(void);

// ==================== UI 框架句柄 ====================
typedef struct {
    lv_obj_t *status_bar;       // 状态栏
    lv_obj_t *wifi_label;       // WiFi 信号标签
    lv_obj_t *battery_label;    // 电量标签
    lv_obj_t *time_label;       // 时间标签
    lv_obj_t *app_grid;         // 应用图标容器
    lv_obj_t *app_icons[UI_APP_MAX];  // 应用图标
    lv_obj_t *app_labels[UI_APP_MAX]; // 应用标签
    ui_app_callback_t callbacks[UI_APP_MAX];  // 回调函数
} ui_framework_t;

// ==================== API 函数 ====================

/**
 * @brief 初始化 UI 框架
 * @return esp_err_t
 */
esp_err_t ui_framework_init(void);

/**
 * @brief 获取 UI 框架句柄
 * @return ui_framework_t*
 */
ui_framework_t* ui_framework_get_handle(void);

/**
 * @brief 更新 WiFi 信号显示
 * @param rssi WiFi RSSI 值
 */
void ui_update_wifi_signal(int rssi);

/**
 * @brief 更新电量显示
 * @param percent 电量百分比 (0-100)
 */
void ui_update_battery(uint8_t percent);

/**
 * @brief 更新时间显示
 * @param hour 小时
 * @param minute 分钟
 */
void ui_update_time(uint8_t hour, uint8_t minute);

/**
 * @brief 注册应用点击回调
 * @param app_id 应用 ID
 * @param callback 回调函数
 */
void ui_register_app_callback(ui_app_id_t app_id, ui_app_callback_t callback);

/**
 * @brief 设置应用图标
 * @param app_id 应用 ID
 * @param icon_data LVGL 图标数据 (NULL 使用默认图标)
 */
void ui_set_app_icon(ui_app_id_t app_id, const void *icon_data);

/**
 * @brief 设置应用名称
 * @param app_id 应用 ID
 * @param name 应用名称
 */
void ui_set_app_name(ui_app_id_t app_id, const char *name);

#ifdef __cplusplus
}
#endif
