#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NAV_STATE_BOOT,
    NAV_STATE_HOME,
    NAV_STATE_MENU,
    NAV_STATE_APP,
} nav_state_t;

typedef struct {
    const char *name;
    const void *icon_img;                  /* LVGL image descriptor pointer */
    void (*create)(lv_obj_t *parent);
    void (*destroy)(void);
    void (*resume)(void);
} app_entry_t;

void app_framework_init(void);
nav_state_t app_framework_get_state(void);
void app_framework_go_home(void);
void app_framework_go_menu(void);
void app_framework_launch_app(int index);
void app_framework_go_back(void);
void app_framework_handle_swipe(lv_dir_t dir);
int  app_framework_get_app_count(void);
const app_entry_t *app_framework_get_app(int index);

/* Screen sleep/wake (AMOLED: off = true black, pixels off) */
void app_framework_screen_off(void);
void app_framework_screen_on(void);

#ifdef __cplusplus
}
#endif
