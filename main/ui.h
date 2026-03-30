/*
 * ui.h - LVGL 用户界面
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建主界面
 */
void ui_create(void);

/**
 * @brief 更新状态栏信息
 */
void ui_update_time(const char *time_str);
void ui_update_wifi_status(bool connected, const char *ssid);

#ifdef __cplusplus
}
#endif
