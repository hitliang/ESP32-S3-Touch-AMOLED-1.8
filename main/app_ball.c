#include "app_ball.h"
#include "sys_imu.h"
#include "lvgl.h"
#include <math.h>
#include <stdlib.h>

#define NUM_BALLS   7
#define W           368
#define H           390
#define G_SCALE     800.0f    /* gravity strength multiplier */
#define DAMPING     0.995f    /* velocity decay per frame */
#define WALL_BOUNCE 0.6f      /* energy retained on wall bounce */

static lv_obj_t *root = NULL;
static lv_obj_t *balls[NUM_BALLS];
static float bx[NUM_BALLS], by[NUM_BALLS];
static float bvx[NUM_BALLS], bvy[NUM_BALLS];
static float br[NUM_BALLS];
static lv_timer_t *timer = NULL;

static const uint32_t colors[] = {
    0xff4444, 0x44cc44, 0x4488ff, 0xffaa00, 0xcc44cc, 0x00cccc, 0xff8888
};
static const float radii[] = { 18, 22, 16, 24, 14, 20, 16 };

static void update_cb(lv_timer_t *t)
{
    sys_imu_data_t imu;
    sys_imu_get_data(&imu);

    /* Gravity from board tilt: ax = left/right, ay = forward/back */
    float gx = imu.accel_y * G_SCALE;
    float gy = imu.accel_x * G_SCALE;

    float dt = 0.016f;  /* ~60fps step */

    for (int i = 0; i < NUM_BALLS; i++) {
        /* Apply gravity */
        bvx[i] += gx * dt;
        bvy[i] += gy * dt;

        /* Damping */
        bvx[i] *= DAMPING;
        bvy[i] *= DAMPING;

        /* Update position */
        bx[i] += bvx[i] * dt;
        by[i] += bvy[i] * dt;

        /* Wall collisions */
        float r = br[i];
        if (bx[i] < r)      { bx[i] = r;      bvx[i] = -bvx[i] * WALL_BOUNCE; }
        if (bx[i] > W - r)  { bx[i] = W - r;  bvx[i] = -bvx[i] * WALL_BOUNCE; }
        if (by[i] < r)      { by[i] = r;      bvy[i] = -bvy[i] * WALL_BOUNCE; }
        if (by[i] > H - r)  { by[i] = H - r;  bvy[i] = -bvy[i] * WALL_BOUNCE; }

        /* Clamp velocity */
        float max_v = 600.0f;
        if (bvx[i] > max_v) bvx[i] = max_v;
        if (bvx[i] < -max_v) bvx[i] = -max_v;
        if (bvy[i] > max_v) bvy[i] = max_v;
        if (bvy[i] < -max_v) bvy[i] = -max_v;
    }

    /* Ball-ball collisions */
    for (int i = 0; i < NUM_BALLS; i++) {
        for (int j = i + 1; j < NUM_BALLS; j++) {
            float dx = bx[j] - bx[i];
            float dy = by[j] - by[i];
            float dist = sqrtf(dx * dx + dy * dy);
            float min_dist = br[i] + br[j];
            if (dist < min_dist && dist > 0.001f) {
                /* Separate */
                float overlap = min_dist - dist;
                float nx = dx / dist;
                float ny = dy / dist;
                bx[i] -= nx * overlap * 0.5f;
                by[i] -= ny * overlap * 0.5f;
                bx[j] += nx * overlap * 0.5f;
                by[j] += ny * overlap * 0.5f;

                /* Elastic collision response */
                float dvx = bvx[i] - bvx[j];
                float dvy = bvy[i] - bvy[j];
                float dvn = dvx * nx + dvy * ny;
                if (dvn > 0) {
                    float impulse = dvn * 0.8f;
                    bvx[i] -= impulse * nx;
                    bvy[i] -= impulse * ny;
                    bvx[j] += impulse * nx;
                    bvy[j] += impulse * ny;
                }
            }
        }
    }

    /* Update LVGL objects */
    for (int i = 0; i < NUM_BALLS; i++) {
        float r = br[i];
        lv_obj_set_pos(balls[i], (int)(bx[i] - r), (int)(by[i] - r));
    }
}

static void create(lv_obj_t *parent)
{
    root = lv_obj_create(parent);
    lv_obj_set_size(root, W, H);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x0a0a18), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    /* Create balls at random positions */
    srand(42);
    for (int i = 0; i < NUM_BALLS; i++) {
        br[i] = radii[i];
        bx[i] = br[i] + (W - 2 * br[i]) * (float)rand() / RAND_MAX;
        by[i] = br[i] + (H - 2 * br[i]) * (float)rand() / RAND_MAX;
        bvx[i] = 0;
        bvy[i] = 0;

        balls[i] = lv_obj_create(root);
        lv_obj_set_size(balls[i], (int)(br[i] * 2), (int)(br[i] * 2));
        lv_obj_set_pos(balls[i], (int)(bx[i] - br[i]), (int)(by[i] - br[i]));
        lv_obj_set_style_bg_color(balls[i], lv_color_hex(colors[i]), 0);
        lv_obj_set_style_radius(balls[i], (int)br[i], 0);
        lv_obj_set_style_border_width(balls[i], 0, 0);
        lv_obj_set_style_shadow_color(balls[i], lv_color_hex(colors[i]), 0);
        lv_obj_set_style_shadow_width(balls[i], 8, 0);
        lv_obj_clear_flag(balls[i], LV_OBJ_FLAG_CLICKABLE);
    }

    /* Update at ~60fps */
    timer = lv_timer_create(update_cb, 16, NULL);
}

static void destroy(void)
{
    if (timer) { lv_timer_del(timer); timer = NULL; }
    if (root) { lv_obj_del(root); root = NULL; }
}

static void resume(void) {}

const app_entry_t app_ball = {
    .name = "Ball Physics",
    .create = create, .destroy = destroy, .resume = resume,
};
