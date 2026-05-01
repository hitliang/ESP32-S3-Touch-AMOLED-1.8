#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*sys_gesture_cb_t)(lv_dir_t dir);

void sys_display_init(void);
lv_disp_t *sys_display_get_disp(void);
SemaphoreHandle_t sys_display_get_mutex(void);
bool sys_display_lock(int timeout_ms);
void sys_display_unlock(void);
void sys_display_set_gesture_cb(sys_gesture_cb_t cb);

#ifdef __cplusplus
}
#endif
