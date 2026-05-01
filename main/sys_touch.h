#pragma once

#include "lvgl.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

void sys_touch_init(lv_disp_t *disp);
esp_lcd_touch_handle_t sys_touch_get_handle(void);
bool sys_touch_get_swipe(lv_dir_t *dir);

#ifdef __cplusplus
}
#endif
