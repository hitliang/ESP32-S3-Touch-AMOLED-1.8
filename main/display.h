/*
 * display.h - SH8601 AMOLED 显示驱动 (LVGL 接口)
 */

#pragma once

#include "lvgl.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化显示屏和 LVGL
 */
esp_err_t display_init(void);

/**
 * @brief 获取 LVGL display 对象 (LVGL 9)
 */
lv_disp_t *display_get(void);

/**
 * @brief 设置屏幕亮度 (0-255)
 */
esp_err_t display_set_brightness(uint8_t brightness);

/**
 * @brief Lock LVGL mutex
 */
bool display_lock(uint32_t timeout_ms);

/**
 * @brief Unlock LVGL mutex
 */
void display_unlock(void);

#ifdef __cplusplus
}
#endif
