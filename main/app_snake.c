#include "app_snake.h"
#include "sys_imu.h"
#include "lvgl.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define COLS      24
#define ROWS      24
#define CELL      14
#define BOARD_W   (COLS * CELL)
#define BOARD_H   (ROWS * CELL)
#define MAX_LEN   (COLS * ROWS)

static lv_obj_t *root = NULL;
static lv_obj_t *canvas = NULL;
static lv_obj_t *lbl_score = NULL;
static lv_timer_t *timer = NULL;
static lv_color_t *cbuf = NULL;

static int snake_x[MAX_LEN], snake_y[MAX_LEN];
static int len, dir, next_dir;
static int food_x, food_y;
static int score;
static bool game_over;
static int tick_count;

enum { DIR_UP=0, DIR_DOWN=1, DIR_LEFT=2, DIR_RIGHT=3 };

static void spawn_food(void)
{
    bool on_snake;
    do {
        on_snake = false;
        food_x = rand() % COLS;
        food_y = rand() % ROWS;
        for (int i = 0; i < len; i++) {
            if (snake_x[i] == food_x && snake_y[i] == food_y) {
                on_snake = true;
                break;
            }
        }
    } while (on_snake);
}

static void reset_game(void)
{
    len = 3;
    snake_x[0] = COLS / 2;     snake_y[0] = ROWS / 2;
    snake_x[1] = COLS / 2 - 1; snake_y[1] = ROWS / 2;
    snake_x[2] = COLS / 2 - 2; snake_y[2] = ROWS / 2;
    dir = DIR_RIGHT;
    next_dir = DIR_RIGHT;
    score = 0;
    game_over = false;
    tick_count = 0;
    spawn_food();
}

static void draw_board(void)
{
    /* Clear to dark bg */
    memset(cbuf, 0, BOARD_W * BOARD_H * sizeof(lv_color_t));

    /* Draw grid lines (subtle) */
    uint16_t grid_color = lv_color_hex(0x111122).full;
    for (int i = 0; i <= COLS; i++) {
        int x = i * CELL;
        for (int y = 0; y < BOARD_H; y++) {
            int idx = y * BOARD_W + x;
            if (idx < BOARD_W * BOARD_H) cbuf[idx].full = grid_color;
        }
    }
    for (int i = 0; i <= ROWS; i++) {
        int y = i * CELL;
        for (int x = 0; x < BOARD_W; x++) {
            int idx = y * BOARD_W + x;
            if (idx < BOARD_W * BOARD_H) cbuf[idx].full = grid_color;
        }
    }

    /* Food - bright red with glow */
    for (int dy = 0; dy < CELL - 1; dy++) {
        for (int dx = 0; dx < CELL - 1; dx++) {
            int idx = (food_y * CELL + dy) * BOARD_W + (food_x * CELL + dx);
            /* pulsing effect */
            int d = (dx - CELL/2) * (dx - CELL/2) + (dy - CELL/2) * (dy - CELL/2);
            uint32_t c = (d < 25) ? lv_color_hex(0xff4444).full : lv_color_hex(0xcc2222).full;
            cbuf[idx].full = c;
        }
    }

    /* Snake body - gradient from head to tail */
    for (int i = len - 1; i >= 0; i--) {
        float t = (float)i / (float)len;
        uint8_t g = 100 + (uint8_t)(155 * t);
        uint8_t b = 50 + (uint8_t)(205 * t);
        uint16_t color = lv_color_hex(((uint32_t)0x00 << 16) | ((uint32_t)g << 8) | b).full;

        for (int dy = 1; dy < CELL - 2; dy++) {
            for (int dx = 1; dx < CELL - 2; dx++) {
                int idx = (snake_y[i] * CELL + dy) * BOARD_W + (snake_x[i] * CELL + dx);
                if (idx < BOARD_W * BOARD_H) cbuf[idx].full = color;
            }
        }
    }

    /* Snake head (brighter) */
    int hx = snake_x[0], hy = snake_y[0];
    for (int dy = 0; dy < CELL - 1; dy++) {
        for (int dx = 0; dx < CELL - 1; dx++) {
            int idx = (hy * CELL + dy) * BOARD_W + (hx * CELL + dx);
            if (idx < BOARD_W * BOARD_H) cbuf[idx].full = lv_color_hex(0x44ee44).full;
        }
    }

    lv_obj_invalidate(canvas);  /* force refresh */
}

static void update_cb(lv_timer_t *t)
{
    if (game_over) return;

    sys_imu_data_t imu;
    sys_imu_get_data(&imu);

    /* Gravity direction: map tilt to snake direction */
    float ax = imu.accel_y;   /* left/right tilt */
    float ay = imu.accel_x;   /* forward/back tilt */

    /* Only change direction when tilt is significant (>0.15G) */
    if (fabsf(ax) > 0.2f || fabsf(ay) > 0.2f) {
        if (fabsf(ay) > fabsf(ax)) {
            next_dir = (ay > 0) ? DIR_DOWN : DIR_UP;
        } else {
            next_dir = (ax > 0) ? DIR_RIGHT : DIR_LEFT;
        }
    }

    /* Move snake every ~4 ticks (~240ms at 60fps) */
    tick_count++;
    if (tick_count < 4) {
        draw_board();
        return;
    }
    tick_count = 0;

    /* Apply direction (prevent reverse) */
    if (next_dir == DIR_UP    && dir != DIR_DOWN)  dir = DIR_UP;
    if (next_dir == DIR_DOWN  && dir != DIR_UP)    dir = DIR_DOWN;
    if (next_dir == DIR_LEFT  && dir != DIR_RIGHT) dir = DIR_LEFT;
    if (next_dir == DIR_RIGHT && dir != DIR_LEFT)  dir = DIR_RIGHT;

    /* Move body */
    for (int i = len - 1; i > 0; i--) {
        snake_x[i] = snake_x[i - 1];
        snake_y[i] = snake_y[i - 1];
    }

    /* Move head */
    if (dir == DIR_UP)    snake_y[0]--;
    if (dir == DIR_DOWN)  snake_y[0]++;
    if (dir == DIR_LEFT)  snake_x[0]--;
    if (dir == DIR_RIGHT) snake_x[0]++;

    /* Wall collision → game over */
    if (snake_x[0] < 0 || snake_x[0] >= COLS || snake_y[0] < 0 || snake_y[0] >= ROWS) {
        game_over = true;
    }

    /* Self collision */
    for (int i = 1; i < len; i++) {
        if (snake_x[0] == snake_x[i] && snake_y[0] == snake_y[i]) {
            game_over = true;
        }
    }

    /* Food */
    if (snake_x[0] == food_x && snake_y[0] == food_y) {
        if (len < MAX_LEN) {
            snake_x[len] = snake_x[len - 1];
            snake_y[len] = snake_y[len - 1];
            len++;
        }
        score += 10;
        spawn_food();
    }

    lv_label_set_text_fmt(lbl_score, "Score: %d", score);
    draw_board();

    if (game_over) {
        lv_obj_t *go = lv_label_create(root);
        lv_label_set_text(go, "GAME OVER\nTap to restart");
        lv_obj_set_style_text_align(go, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(go, lv_color_hex(0xff4444), 0);
        lv_obj_set_style_text_font(go, &lv_font_montserrat_24, 0);
        lv_obj_center(go);
    }
}

static void on_tap(lv_event_t *e)
{
    if (game_over) {
        /* Clean up game over labels */
        lv_obj_clean(root);
        lv_obj_t *l = lv_label_create(root);
        lv_label_set_text_fmt(l, "Score: %d", score);
        lv_obj_set_style_text_color(l, lv_color_hex(0x8888cc), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 5);
        lbl_score = l;
        reset_game();
    }
}

static void create(lv_obj_t *parent)
{
    root = lv_obj_create(parent);
    lv_obj_set_size(root, 340, 380);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    /* Score label */
    lbl_score = lv_label_create(root);
    lv_label_set_text(lbl_score, "Score: 0");
    lv_obj_set_style_text_color(lbl_score, lv_color_hex(0x8888cc), 0);
    lv_obj_set_style_text_font(lbl_score, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_score, LV_ALIGN_TOP_MID, 0, 5);

    /* Canvas for game board */
    canvas = lv_canvas_create(root);
    lv_obj_set_size(canvas, BOARD_W, BOARD_H);
    lv_obj_align(canvas, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_all(canvas, 0, 0);

    cbuf = heap_caps_malloc(BOARD_W * BOARD_H * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    assert(cbuf);
    lv_canvas_set_buffer(canvas, cbuf, BOARD_W, BOARD_H, LV_IMG_CF_TRUE_COLOR);

    /* Tap to restart on game over */
    lv_obj_add_event_cb(root, on_tap, LV_EVENT_CLICKED, NULL);

    reset_game();
    timer = lv_timer_create(update_cb, 60, NULL);
}

static void destroy(void)
{
    if (timer) { lv_timer_del(timer); timer = NULL; }
    if (root) { lv_obj_del(root); root = NULL; }
    if (cbuf) { heap_caps_free(cbuf); cbuf = NULL; }
}

static void resume(void) {}

const app_entry_t app_snake = {
    .name = "Snake",
    .create = create, .destroy = destroy, .resume = resume,
};
