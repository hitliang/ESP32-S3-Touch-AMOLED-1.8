#include "app_attitude.h"
#include "sys_imu.h"
#include "lvgl.h"
#include <math.h>
#include <stdio.h>

static lv_obj_t *root   = NULL;
static lv_obj_t *arc_roll  = NULL;
static lv_obj_t *arc_pitch = NULL;
static lv_obj_t *lbl_roll  = NULL;
static lv_obj_t *lbl_pitch = NULL;
static lv_obj_t *bubble_container = NULL;
static lv_obj_t *bubble_dot = NULL;
static lv_timer_t *update_timer = NULL;

#define BUBBLE_R    90
#define DOT_R       12

static void update_cb(lv_timer_t *t)
{
    sys_imu_data_t imu;
    sys_imu_get_data(&imu);

    float roll  = imu.roll;
    float pitch = imu.pitch;

    /* Roll arc: -90..+90, vertical arc on left */
    int r_val = (int)roll + 90;  /* map to 0..180 for arc */
    if (r_val < 0) r_val = 0;
    if (r_val > 180) r_val = 180;
    lv_arc_set_value(arc_roll, r_val);

    /* Pitch arc: -90..+90, vertical arc on right */
    int p_val = (int)pitch + 90;
    if (p_val < 0) p_val = 0;
    if (p_val > 180) p_val = 180;
    lv_arc_set_value(arc_pitch, p_val);

    lv_label_set_text_fmt(lbl_roll,  "R: %+.1f", roll);
    lv_label_set_text_fmt(lbl_pitch, "P: %+.1f", pitch);

    /* Bubble level: dot moves opposite to tilt */
    int dx = (int)(-roll  * 1.5f);
    int dy = (int)( pitch * 1.5f);
    if (dx > BUBBLE_R - DOT_R) dx = BUBBLE_R - DOT_R;
    if (dx < -(BUBBLE_R - DOT_R)) dx = -(BUBBLE_R - DOT_R);
    if (dy > BUBBLE_R - DOT_R) dy = BUBBLE_R - DOT_R;
    if (dy < -(BUBBLE_R - DOT_R)) dy = -(BUBBLE_R - DOT_R);
    lv_obj_set_pos(bubble_dot, dx + BUBBLE_R - DOT_R, dy + BUBBLE_R - DOT_R);
}

static void create(lv_obj_t *parent)
{
    root = lv_obj_create(parent);
    lv_obj_set_size(root, 340, 390);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);

    /* ---- Roll arc (left side, vertical half-circle) ---- */
    arc_roll = lv_arc_create(root);
    lv_obj_set_size(arc_roll, 140, 140);
    lv_obj_align(arc_roll, LV_ALIGN_TOP_LEFT, 15, 10);
    lv_arc_set_rotation(arc_roll, 270);
    lv_arc_set_range(arc_roll, 0, 180);
    lv_arc_set_bg_angles(arc_roll, 0, 180);
    lv_obj_set_style_arc_color(arc_roll, lv_color_hex(0x4488cc), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_roll, lv_color_hex(0x222244), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_roll, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_roll, 8, LV_PART_MAIN);
    lv_obj_remove_style(arc_roll, NULL, LV_PART_KNOB);

    lbl_roll = lv_label_create(root);
    lv_label_set_text(lbl_roll, "R: +0.0");
    lv_obj_set_style_text_color(lbl_roll, lv_color_hex(0x4488cc), 0);
    lv_obj_align(lbl_roll, LV_ALIGN_TOP_LEFT, 45, 130);

    /* Roll label */
    lv_obj_t *lt = lv_label_create(root);
    lv_label_set_text(lt, "ROLL");
    lv_obj_set_style_text_font(lt, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lt, lv_color_hex(0x666688), 0);
    lv_obj_align(lt, LV_ALIGN_TOP_LEFT, 55, 5);

    /* ---- Pitch arc (right side) ---- */
    arc_pitch = lv_arc_create(root);
    lv_obj_set_size(arc_pitch, 140, 140);
    lv_obj_align(arc_pitch, LV_ALIGN_TOP_RIGHT, -15, 10);
    lv_arc_set_rotation(arc_pitch, 270);
    lv_arc_set_range(arc_pitch, 0, 180);
    lv_arc_set_bg_angles(arc_pitch, 0, 180);
    lv_obj_set_style_arc_color(arc_pitch, lv_color_hex(0xcc8844), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_pitch, lv_color_hex(0x442222), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_pitch, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_pitch, 8, LV_PART_MAIN);
    lv_obj_remove_style(arc_pitch, NULL, LV_PART_KNOB);

    lbl_pitch = lv_label_create(root);
    lv_label_set_text(lbl_pitch, "P: +0.0");
    lv_obj_set_style_text_color(lbl_pitch, lv_color_hex(0xcc8844), 0);
    lv_obj_align(lbl_pitch, LV_ALIGN_TOP_RIGHT, -45, 130);

    lt = lv_label_create(root);
    lv_label_set_text(lt, "PITCH");
    lv_obj_set_style_text_font(lt, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lt, lv_color_hex(0x666688), 0);
    lv_obj_align(lt, LV_ALIGN_TOP_RIGHT, -45, 5);

    /* ---- Bubble level (bottom) ---- */
    bubble_container = lv_obj_create(root);
    lv_obj_set_size(bubble_container, BUBBLE_R * 2 + 4, BUBBLE_R * 2 + 4);
    lv_obj_align(bubble_container, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(bubble_container, lv_color_hex(0x111122), 0);
    lv_obj_set_style_border_color(bubble_container, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(bubble_container, 2, 0);
    lv_obj_set_style_radius(bubble_container, BUBBLE_R + 2, 0);

    /* Crosshair */
    lv_obj_t *hline = lv_obj_create(bubble_container);
    lv_obj_set_size(hline, BUBBLE_R * 2, 1);
    lv_obj_center(hline);
    lv_obj_set_style_bg_color(hline, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(hline, 0, 0);

    lv_obj_t *vline = lv_obj_create(bubble_container);
    lv_obj_set_size(vline, 1, BUBBLE_R * 2);
    lv_obj_center(vline);
    lv_obj_set_style_bg_color(vline, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(vline, 0, 0);

    /* Moving dot */
    bubble_dot = lv_obj_create(bubble_container);
    lv_obj_set_size(bubble_dot, DOT_R * 2, DOT_R * 2);
    lv_obj_set_style_bg_color(bubble_dot, lv_color_hex(0x44cc88), 0);
    lv_obj_set_style_radius(bubble_dot, DOT_R, 0);
    lv_obj_set_style_border_width(bubble_dot, 0, 0);
    lv_obj_set_style_shadow_color(bubble_dot, lv_color_hex(0x44cc88), 0);
    lv_obj_set_style_shadow_width(bubble_dot, 12, 0);
    lv_obj_set_pos(bubble_dot, BUBBLE_R - DOT_R, BUBBLE_R - DOT_R);

    /* Level label */
    lt = lv_label_create(root);
    lv_label_set_text(lt, "BUBBLE LEVEL");
    lv_obj_set_style_text_font(lt, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lt, lv_color_hex(0x666688), 0);
    lv_obj_align(lt, LV_ALIGN_BOTTOM_MID, 0, -10 - BUBBLE_R * 2 - 20);

    /* Update timer at ~15Hz */
    update_timer = lv_timer_create(update_cb, 66, NULL);
}

static void destroy(void)
{
    if (update_timer) { lv_timer_del(update_timer); update_timer = NULL; }
    if (root) { lv_obj_del(root); root = NULL; }
}

static void resume(void) {}

const app_entry_t app_attitude = {
    .name = "Attitude",
    .create = create, .destroy = destroy, .resume = resume,
};
