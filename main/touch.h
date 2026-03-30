/*
 * touch.h - FT3168 触摸驱动
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 FT3168 触摸驱动并注册到 LVGL
 */
esp_err_t touch_init(void);

#ifdef __cplusplus
}
#endif
