#include "app_framework.h"
#include "ui_home.h"
#include "ui_menu.h"
#include "sys_display.h"
#include "sys_touch.h"
#include "esp_log.h"

static const char *TAG = "app_fw";

/* ------------------------------------------------------------------ */
/*  App stubs (defined in their own .c files, not yet created)         */
/*  We declare them as weak externs so Phase 1 builds without them.    */
/* ------------------------------------------------------------------ */
#define DECLARE_APP(name) \
    extern const app_entry_t app_##name __attribute__((weak))

DECLARE_APP(settings);
DECLARE_APP(attitude);
DECLARE_APP(weather);
DECLARE_APP(voice);
DECLARE_APP(music);
DECLARE_APP(metronome);
DECLARE_APP(pedometer);
DECLARE_APP(ball);

/* Dummy app for un-implemented apps */
static void app_dummy_create(lv_obj_t *parent) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, "Coming Soon");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}
static void app_dummy_destroy(void) {}
static void app_dummy_resume(void) {}

static const app_entry_t app_dummy = {
    .name = "Coming Soon",
    .icon_img = NULL,
    .create = app_dummy_create, .destroy = app_dummy_destroy, .resume = app_dummy_resume,
};

/* ------------------------------------------------------------------ */
/*  App registry (filled at runtime)                                    */
/* ------------------------------------------------------------------ */
#define MAX_APPS 16

static const app_entry_t *app_registry[MAX_APPS + 1];
static int app_count = 0;

static void app_registry_add(const app_entry_t *candidate)
{
    if (candidate && candidate->name) {
        app_registry[app_count++] = candidate;
    } else {
        app_registry[app_count++] = &app_dummy;
    }
}

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
    /* Build app registry at runtime */
    app_registry_add(&app_settings);
    app_registry_add(&app_attitude);
    app_registry_add(&app_weather);
    app_registry_add(&app_voice);
    app_registry_add(&app_music);
    app_registry_add(&app_metronome);
    app_registry_add(&app_pedometer);
    app_registry_add(&app_ball);

    /* 9th slot: placeholder to complete 3x3 grid */
    static const app_entry_t app_extra = {
        .name = "More",
        .create = app_dummy_create, .destroy = app_dummy_destroy, .resume = app_dummy_resume,
    };
    app_registry_add(&app_extra);

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
    if (current_app) {
        current_app->destroy();
        current_app = NULL;
    }
    if (scr_app) {
        lv_obj_del(scr_app);
        scr_app = NULL;
    }
    nav_state = NAV_STATE_HOME;
    load_screen_impl(scr_home, true);
    ui_home_update();
}

void app_framework_go_menu(void)
{
    nav_state = NAV_STATE_MENU;
    load_screen_impl(scr_menu, true);
}

void app_framework_launch_app(int index)
{
    if (index < 0 || index >= app_count) return;
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

    /* Back button */
    lv_obj_t *btn_back = lv_btn_create(scr_app);
    lv_obj_set_size(btn_back, 50, 30);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 5, 5);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " Back");
    lv_obj_add_event_cb(btn_back, (lv_event_cb_t)app_framework_go_back, LV_EVENT_CLICKED, NULL);

    /* App title bar */
    lv_obj_t *title = lv_label_create(scr_app);
    lv_label_set_text(title, current_app->name);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* App content area */
    app_content = lv_obj_create(scr_app);
    lv_obj_set_size(app_content, 368, 400);
    lv_obj_align(app_content, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_width(app_content, 0, 0);
    lv_obj_set_style_bg_opa(app_content, LV_OPA_TRANSP, 0);

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
    if (current_app) {
        current_app->destroy();
        current_app = NULL;
    }
    if (scr_app) {
        lv_obj_del(scr_app);
        scr_app = NULL;
    }
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
