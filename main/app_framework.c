#include "app_framework.h"
#include "app_settings.h"
#include "app_attitude.h"
#include "ui_home.h"
#include "ui_menu.h"
#include "sys_display.h"
#include "sys_touch.h"
#include "esp_log.h"

static const char *TAG = "app_fw";

/* Dummy app for not-yet-implemented apps */
static void app_dummy_create(lv_obj_t *parent) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, "Coming Soon");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}
static void app_dummy_destroy(void) {}
static void app_dummy_resume(void) {}

#define APP(name)  &app_##name
#define PLACEHOLDER(name_str)  &(const app_entry_t){ \
    .name = name_str, \
    .create = app_dummy_create, .destroy = app_dummy_destroy, .resume = app_dummy_resume, \
}

/* ------------------------------------------------------------------ */
/*  App registry                                                        */
/* ------------------------------------------------------------------ */
#define MAX_APPS 16

static const app_entry_t *app_registry[MAX_APPS + 1];
static int app_count = 0;

/* ------------------------------------------------------------------ */
/*  Navigation state                                                   */
/* ------------------------------------------------------------------ */
static nav_state_t nav_state = NAV_STATE_BOOT;
static const app_entry_t *current_app = NULL;
lv_obj_t *scr_home = NULL;
lv_obj_t *scr_menu = NULL;
static lv_obj_t *scr_app = NULL;
static lv_obj_t *scr_blank = NULL;
static lv_obj_t *app_content = NULL;

/* ------------------------------------------------------------------ */
/*  Forward decls                                                      */
/* ------------------------------------------------------------------ */
static void load_screen_impl(lv_obj_t *scr, bool animate);

/* ------------------------------------------------------------------ */
/*  Implementation                                                     */
/* ------------------------------------------------------------------ */
void app_framework_init(void)
{
    /* Build app registry: real apps first, placeholders for rest */
    int idx = 0;
    app_registry[idx++] = APP(settings);
    app_registry[idx++] = APP(attitude);
    app_registry[idx++] = PLACEHOLDER("Weather");
    app_registry[idx++] = PLACEHOLDER("Voice AI");
    app_registry[idx++] = PLACEHOLDER("Music");
    app_registry[idx++] = PLACEHOLDER("Metronome");
    app_registry[idx++] = PLACEHOLDER("Pedometer");
    app_registry[idx++] = PLACEHOLDER("Ball");
    app_registry[idx++] = PLACEHOLDER("More");
    app_count = idx;

    /* Create all screens upfront */
    scr_blank = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_blank, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr_blank, LV_OPA_COVER, 0);

    scr_home = lv_obj_create(NULL);
    scr_menu = lv_obj_create(NULL);

    ui_home_create(scr_home);
    ui_menu_create(scr_menu);

    ESP_LOGI(TAG, "Framework ready, %d apps registered", app_count);
}

nav_state_t app_framework_get_state(void)
{
    return nav_state;
}

void app_framework_go_home(void)
{
    /* Just switch screen, don't delete — safe to call from any context */
    nav_state = NAV_STATE_HOME;
    load_screen_impl(scr_home, false);
    ui_home_update();
}

void app_framework_go_menu(void)
{
    nav_state = NAV_STATE_MENU;
    load_screen_impl(scr_menu, false);
}

void app_framework_launch_app(int index)
{
    if (index < 0 || index >= app_count) return;

    /* Clean up old app if re-entering */
    if (current_app) {
        current_app->destroy();
        current_app = NULL;
    }
    if (scr_app) {
        lv_obj_del(scr_app);
        scr_app = NULL;
    }

    current_app = app_registry[index];
    scr_app = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_app, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr_app, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr_app, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr_app, LV_SCROLLBAR_MODE_OFF);

    /* Title bar */
    lv_obj_t *title = lv_label_create(scr_app);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x8888cc), 0);
    lv_label_set_text(title, current_app->name);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* App content area */
    app_content = lv_obj_create(scr_app);
    lv_obj_set_size(app_content, 368, 400);
    lv_obj_align(app_content, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_width(app_content, 0, 0);
    lv_obj_set_style_bg_opa(app_content, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(app_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(app_content, LV_SCROLLBAR_MODE_OFF);

    current_app->create(app_content);

    nav_state = NAV_STATE_APP;
    load_screen_impl(scr_app, true);
}

void app_framework_go_back(void)
{
    if (nav_state == NAV_STATE_APP) {
        if (current_app) {
            current_app->destroy();
            current_app = NULL;
        }
        if (scr_app) {
            lv_obj_del(scr_app);
            scr_app = NULL;
        }
        app_framework_go_menu();
    }
}

void app_framework_handle_swipe(lv_dir_t dir)
{
    switch (nav_state) {
    case NAV_STATE_HOME:
        if (dir == LV_DIR_TOP) app_framework_go_menu();
        break;
    case NAV_STATE_MENU:
        if (dir == LV_DIR_BOTTOM) app_framework_go_home();
        break;
    default:
        break;
    }
}

int app_framework_get_app_count(void)
{
    return app_count;
}

const app_entry_t *app_framework_get_app(int index)
{
    if (index < 0 || index >= app_count) return NULL;
    return app_registry[index];
}

void app_framework_screen_off(void)
{
    nav_state = NAV_STATE_HOME;
    load_screen_impl(scr_blank, false);
    sys_touch_set_enabled(false);
}

void app_framework_screen_on(void)
{
    sys_touch_set_enabled(true);
    app_framework_go_home();
}

/* ------------------------------------------------------------------ */
/*  Screen loader helper                                               */
/* ------------------------------------------------------------------ */
static void load_screen_impl(lv_obj_t *scr, bool animate)
{
    if (!scr || scr == lv_scr_act()) return;
    if (animate) {
        lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
    } else {
        lv_scr_load(scr);
    }
}
