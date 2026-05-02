#include "app_attitude.h"
#include "sys_imu.h"
#include "lvgl.h"
#include <math.h>
#include <stdio.h>

static lv_obj_t *root = NULL;
static lv_obj_t *arc_roll = NULL, *arc_pitch = NULL;
static lv_obj_t *lbl_roll = NULL, *lbl_pitch = NULL;
static lv_obj_t *lbl_angle = NULL;
static lv_obj_t *bubble_cont = NULL, *bubble_dot = NULL;
static lv_timer_t *update_timer = NULL;

#define BUBBLE_DIAM   160
#define DOT_R          14

static void update_cb(lv_timer_t *t)
{
    sys_imu_data_t imu;
    sys_imu_get_data(&imu);

    float r = imu.roll;
    float p = imu.pitch;

    /* Map -90..+90 to 0..180 for the arc */
    int rv = (int)(r + 90.0f); if (rv < 0) rv = 0; if (rv > 180) rv = 180;
    int pv = (int)(p + 90.0f); if (pv < 0) pv = 0; if (pv > 180) pv = 180;
    lv_arc_set_value(arc_roll, rv);
    lv_arc_set_value(arc_pitch, pv);

    lv_label_set_text_fmt(lbl_roll,  " %+.1f ", r);
    lv_label_set_text_fmt(lbl_pitch, " %+.1f ", p);
    lv_label_set_text_fmt(lbl_angle, "R:%+.1f  P:%+.1f", r, p);

    /* Bubble — use accel directly: ax = left/right tilt, ay = forward/back tilt
       Bubble goes to the HIGH side (opposite to tilt direction) */
    int half = BUBBLE_DIAM / 2;
    int limit = half - DOT_R;
    int dx = (int)( imu.accel_y * half);
    int dy = (int)(-imu.accel_x * half);
    if (dx > limit) dx = limit;
    if (dx < -limit) dx = -limit;
    if (dy > limit) dy = limit;
    if (dy < -limit) dy = -limit;
    lv_obj_set_pos(bubble_dot, dx + half - DOT_R, dy + half - DOT_R);
}

static lv_obj_t *make_arc(lv_obj_t *parent, uint32_t color, uint32_t bg, int align, int ox)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, 150, 150);
    lv_obj_align(arc, align, ox, 35);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_range(arc, 0, 180);
    lv_arc_set_bg_angles(arc, 0, 180);
    lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 10, LV_PART_MAIN);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    return arc;
}

static void create(lv_obj_t *parent)
{
    root = lv_obj_create(parent);
    lv_obj_set_size(root, 340, 390);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);

    /* Section title */
    lv_obj_t *t = lv_label_create(root);
    lv_label_set_text(t, "ATTITUDE");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0x556688), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 5);

    /* Roll arc (left) */
    arc_roll = make_arc(root, 0x4488cc, 0x1a2244, LV_ALIGN_TOP_LEFT, 15);
    lbl_roll = lv_label_create(root);
    lv_obj_set_style_text_font(lbl_roll, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_roll, lv_color_hex(0x4488cc), 0);
    lv_label_set_text(lbl_roll, " +0.0 ");
    lv_obj_align(lbl_roll, LV_ALIGN_TOP_LEFT, 35, 165);

    lv_obj_t *lt = lv_label_create(root);
    lv_label_set_text(lt, "ROLL");
    lv_obj_set_style_text_font(lt, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lt, lv_color_hex(0x556688), 0);
    lv_obj_align(lt, LV_ALIGN_TOP_LEFT, 60, 145);

    /* Pitch arc (right) */
    arc_pitch = make_arc(root, 0xcc8844, 0x2a1a14, LV_ALIGN_TOP_RIGHT, -15);
    lbl_pitch = lv_label_create(root);
    lv_obj_set_style_text_font(lbl_pitch, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_pitch, lv_color_hex(0xcc8844), 0);
    lv_label_set_text(lbl_pitch, " +0.0 ");
    lv_obj_align(lbl_pitch, LV_ALIGN_TOP_RIGHT, -35, 165);

    lt = lv_label_create(root);
    lv_label_set_text(lt, "PITCH");
    lv_obj_set_style_text_font(lt, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lt, lv_color_hex(0x556688), 0);
    lv_obj_align(lt, LV_ALIGN_TOP_RIGHT, -55, 145);

    /* Angle readout */
    lbl_angle = lv_label_create(root);
    lv_obj_set_style_text_font(lbl_angle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_angle, lv_color_hex(0x778899), 0);
    lv_label_set_text(lbl_angle, "R:+0.0  P:+0.0");
    lv_obj_align(lbl_angle, LV_ALIGN_TOP_MID, 0, 200);

    /* Bubble level */
    int half = BUBBLE_DIAM / 2;
    bubble_cont = lv_obj_create(root);
    lv_obj_set_size(bubble_cont, BUBBLE_DIAM, BUBBLE_DIAM);
    lv_obj_align(bubble_cont, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_color(bubble_cont, lv_color_hex(0x0a0a18), 0);
    lv_obj_set_style_border_color(bubble_cont, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(bubble_cont, 2, 0);
    lv_obj_set_style_radius(bubble_cont, half, 0);
    lv_obj_set_style_pad_all(bubble_cont, 0, 0);
    lv_obj_clear_flag(bubble_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(bubble_cont, LV_SCROLLBAR_MODE_OFF);

    /* Crosshair lines */
    lv_obj_t *hl = lv_obj_create(bubble_cont);
    lv_obj_set_size(hl, BUBBLE_DIAM - 20, 1);
    lv_obj_center(hl);
    lv_obj_set_style_bg_color(hl, lv_color_hex(0x222244), 0);
    lv_obj_set_style_border_width(hl, 0, 0);
    lv_obj_clear_flag(hl, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *vl = lv_obj_create(bubble_cont);
    lv_obj_set_size(vl, 1, BUBBLE_DIAM - 20);
    lv_obj_center(vl);
    lv_obj_set_style_bg_color(vl, lv_color_hex(0x222244), 0);
    lv_obj_set_style_border_width(vl, 0, 0);
    lv_obj_clear_flag(vl, LV_OBJ_FLAG_CLICKABLE);

    /* Center ring */
    lv_obj_t *ring = lv_obj_create(bubble_cont);
    lv_obj_set_size(ring, 20, 20);
    lv_obj_center(ring);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(0x334466), 0);
    lv_obj_set_style_border_width(ring, 1, 0);
    lv_obj_set_style_radius(ring, 10, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);

    /* Bubble dot */
    bubble_dot = lv_obj_create(bubble_cont);
    lv_obj_set_size(bubble_dot, DOT_R * 2, DOT_R * 2);
    lv_obj_set_style_bg_color(bubble_dot, lv_color_hex(0x44ee88), 0);
    lv_obj_set_style_radius(bubble_dot, DOT_R, 0);
    lv_obj_set_style_border_width(bubble_dot, 0, 0);
    lv_obj_set_style_shadow_color(bubble_dot, lv_color_hex(0x44ee88), 0);
    lv_obj_set_style_shadow_width(bubble_dot, 16, 0);
    lv_obj_clear_flag(bubble_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(bubble_dot, half - DOT_R, half - DOT_R);

    lt = lv_label_create(root);
    lv_label_set_text(lt, "LEVEL");
    lv_obj_set_style_text_font(lt, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lt, lv_color_hex(0x556688), 0);
    lv_obj_align(lt, LV_ALIGN_BOTTOM_MID, 0, -BUBBLE_DIAM - 20);

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
