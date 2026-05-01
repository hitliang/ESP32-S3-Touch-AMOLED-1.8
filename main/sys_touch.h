#pragma once

#include "lvgl.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

void sys_touch_init(lv_disp_t *disp);
esp_lcd_touch_handle_t sys_touch_get_handle(void);
void sys_touch_set_enabled(bool enabled);

/* Raw touch state, exposed for swipe detection */
extern int  g_debug_touch_x;
extern int  g_debug_touch_y;
extern bool g_debug_touch_pressed;

#ifdef __cplusplus
}
#endif
