#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *ui_home_create(lv_obj_t *parent);
void ui_home_update(void);

#ifdef __cplusplus
}
#endif
