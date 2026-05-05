#include "ui_menu.h"
#include "app_framework.h"
#include "esp_log.h"

static const char *TAG = "ui_menu";

#define GRID_COLS   3
#define ICON_SIZE   90
#define TILE_GAP    12
#define TILE_WIDTH  100
#define TILE_HEIGHT 120

/* Simple colored icons for Phase 1 — will be replaced with real images later */
typedef struct {
    const char *symbol;
    uint32_t   color;
} icon_def_t;

static const icon_def_t app_icons[] = {
    {LV_SYMBOL_SETTINGS,  0x666688},   /* Settings    */
    {LV_SYMBOL_GPS,       0x4488cc},   /* Attitude    */
    {LV_SYMBOL_HOME,      0x44aacc},   /* Weather     */
    {LV_SYMBOL_FILE,      0xcc4488},   /* Music       */
    {LV_SYMBOL_CHARGE,    0x88cc44},   /* Snake       */
    {LV_SYMBOL_LIST,      0xccaa44},   /* Pedometer   */
    {LV_SYMBOL_SHUFFLE,   0x44cc88},   /* Ball        */
    {LV_SYMBOL_PLUS,      0x8866cc},   /* More        */
};

static void on_app_tapped(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "App %d tapped", index);
    app_framework_launch_app(index);
}

lv_obj_t *ui_menu_create(lv_obj_t *scr)
{
    /* True AMOLED black background */
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Apps");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xcccccc), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    int n_apps = app_framework_get_app_count();

    /* Grid container */
    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, 368, 390);
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 10, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    int rows = (n_apps + GRID_COLS - 1) / GRID_COLS;
    int needed_h = rows * (TILE_HEIGHT + TILE_GAP) + 20;
    if (needed_h < 390) needed_h = 390;
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < n_apps; i++) {
        const app_entry_t *app = app_framework_get_app(i);
        const icon_def_t *icon = &app_icons[i % (sizeof(app_icons) / sizeof(app_icons[0]))];

        /* Tile container */
        lv_obj_t *tile = lv_btn_create(cont);
        lv_obj_set_size(tile, TILE_WIDTH, TILE_HEIGHT);
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x222244), 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_80, 0);
        lv_obj_set_style_radius(tile, 12, 0);
        lv_obj_set_style_shadow_width(tile, 4, 0);
        lv_obj_set_style_shadow_color(tile, lv_color_hex(0x000033), 0);
        lv_obj_add_event_cb(tile, on_app_tapped, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        /* Icon circle */
        lv_obj_t *icon_obj = lv_obj_create(tile);
        lv_obj_set_size(icon_obj, ICON_SIZE, ICON_SIZE);
        lv_obj_align(icon_obj, LV_ALIGN_TOP_MID, 0, 5);
        lv_obj_set_style_bg_color(icon_obj, lv_color_hex(icon->color), 0);
        lv_obj_set_style_bg_opa(icon_obj, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(icon_obj, ICON_SIZE / 2, 0);
        lv_obj_set_style_border_width(icon_obj, 0, 0);
        lv_obj_clear_flag(icon_obj, LV_OBJ_FLAG_CLICKABLE);  /* don't eat clicks */

        lv_obj_t *sym = lv_label_create(icon_obj);
        lv_label_set_text(sym, icon->symbol);
        lv_obj_center(sym);
        lv_obj_set_style_text_font(sym, &lv_font_montserrat_16, 0);

        /* App name label */
        lv_obj_t *lbl = lv_label_create(tile);
        lv_label_set_text(lbl, app->name);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xcccccc), 0);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -5);
    }

    ESP_LOGI(TAG, "Menu created with %d apps", n_apps);
    return scr;
}
