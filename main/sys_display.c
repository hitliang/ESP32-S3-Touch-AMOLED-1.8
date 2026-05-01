#include "sys_display.h"
#include "sys_i2c.h"
#include "sys_touch.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/spi_master.h"

static const char *TAG = "sys_display";

#define LCD_HOST          SPI2_HOST

#if CONFIG_LV_COLOR_DEPTH == 32
#define LCD_BIT_PER_PIXEL 24
#else
#define LCD_BIT_PER_PIXEL 16
#endif

#define PIN_LCD_CS        GPIO_NUM_12
#define PIN_LCD_PCLK      GPIO_NUM_11
#define PIN_LCD_DATA0     GPIO_NUM_4
#define PIN_LCD_DATA1     GPIO_NUM_5
#define PIN_LCD_DATA2     GPIO_NUM_6
#define PIN_LCD_DATA3     GPIO_NUM_7
#define PIN_LCD_RST       (-1)
#define PIN_BK_LIGHT      (-1)

#define LCD_H_RES         368
#define LCD_V_RES         448

#define LVGL_BUF_HEIGHT   (LCD_V_RES / 4)    /* 112 */
#define LVGL_TICK_MS      2
#define LVGL_TASK_PRIO    2
#define LVGL_TASK_STACK   (4 * 1024)

static SemaphoreHandle_t lvgl_mux = NULL;
static lv_disp_drv_t disp_drv;
static lv_disp_draw_buf_t disp_buf;
static esp_lcd_panel_handle_t panel_handle = NULL;
static sys_gesture_cb_t gesture_cb = NULL;

/* ------------------------------------------------------------------ */
/*  SH8601 init command table                                          */
/* ------------------------------------------------------------------ */
static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x51, (uint8_t[]){0x00}, 1, 10},
    {0x29, (uint8_t[]){0x00}, 0, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
};

/* ------------------------------------------------------------------ */
/*  LVGL flush-ready callback                                          */
/* ------------------------------------------------------------------ */
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_drv_t *drv = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(drv);
    return false;
}

/* ------------------------------------------------------------------ */
/*  LVGL flush callback                                                */
/* ------------------------------------------------------------------ */
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    const int x1 = area->x1, x2 = area->x2, y1 = area->y1, y2 = area->y2;

#if LCD_BIT_PER_PIXEL == 24
    uint8_t *to = (uint8_t *)color_map;
    uint8_t temp = 0;
    uint16_t pixel_num = (x2 - x1 + 1) * (y2 - y1 + 1);
    temp = color_map[0].ch.blue;
    *to++ = color_map[0].ch.red;
    *to++ = color_map[0].ch.green;
    *to++ = temp;
    for (int i = 1; i < pixel_num; i++) {
        *to++ = color_map[i].ch.red;
        *to++ = color_map[i].ch.green;
        *to++ = color_map[i].ch.blue;
    }
#endif

    esp_lcd_panel_draw_bitmap(panel, x1, y1, x2 + 1, y2 + 1, color_map);
}

/* ------------------------------------------------------------------ */
/*  LVGL display rotation callback                                     */
/* ------------------------------------------------------------------ */
static void lvgl_update_cb(lv_disp_drv_t *drv)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    switch (drv->rotated) {
    case LV_DISP_ROT_NONE:
        esp_lcd_panel_swap_xy(panel, false);
        esp_lcd_panel_mirror(panel, true, false);
        break;
    case LV_DISP_ROT_90:
        esp_lcd_panel_swap_xy(panel, true);
        esp_lcd_panel_mirror(panel, true, true);
        break;
    case LV_DISP_ROT_180:
        esp_lcd_panel_swap_xy(panel, false);
        esp_lcd_panel_mirror(panel, false, true);
        break;
    case LV_DISP_ROT_270:
        esp_lcd_panel_swap_xy(panel, true);
        esp_lcd_panel_mirror(panel, false, false);
        break;
    }
}

/* ------------------------------------------------------------------ */
/*  LVGL rounder callback (align to 2 pixels for QSPI)                 */
/* ------------------------------------------------------------------ */
static void lvgl_rounder_cb(lv_disp_drv_t *drv, lv_area_t *area)
{
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

/* ------------------------------------------------------------------ */
/*  LVGL tick timer                                                    */
/* ------------------------------------------------------------------ */
static void lvgl_tick_cb(void *arg)
{
    lv_tick_inc(LVGL_TICK_MS);
}

/* ------------------------------------------------------------------ */
/*  LVGL port task — also handles swipe detection                       */
/* ------------------------------------------------------------------ */
static void lvgl_port_task(void *arg)
{
    uint32_t task_delay_ms = 500;

    /* Swipe detection: track touch position delta */
    int swipe_start_x = -1, swipe_start_y = -1;
    int last_tx = -1, last_ty = -1;
    bool swipe_fired = false;
    int stable_count = 0;

    while (1) {
        if (sys_display_lock(-1)) {
            task_delay_ms = lv_timer_handler();

            int tx = g_debug_touch_x;
            int ty = g_debug_touch_y;
            bool pressed = g_debug_touch_pressed;

            if (pressed && tx >= 0) {
                /* Detect new touch: big position jump or just starting */
                if (last_tx < 0 || abs(tx - last_tx) > 100 || abs(ty - last_ty) > 100) {
                    swipe_start_x = tx;
                    swipe_start_y = ty;
                    swipe_fired = false;
                    stable_count = 0;
                }

                /* Stable counter (for auto-reset) */
                if (abs(tx - last_tx) < 3 && abs(ty - last_ty) < 3) {
                    stable_count++;
                    if (stable_count > 30) {
                        /* Reset after ~120ms of stability */
                        swipe_start_x = tx;
                        swipe_start_y = ty;
                        swipe_fired = false;
                        stable_count = 0;
                    }
                } else {
                    stable_count = 0;
                }

                /* Swipe detection */
                if (!swipe_fired && swipe_start_x >= 0) {
                    int dy = ty - swipe_start_y;
                    if (dy < -40) {
                        if (gesture_cb) gesture_cb(LV_DIR_TOP);
                        swipe_fired = true;
                    } else if (dy > 40) {
                        if (gesture_cb) gesture_cb(LV_DIR_BOTTOM);
                        swipe_fired = true;
                    }
                }

                last_tx = tx; last_ty = ty;
            }

            sys_display_unlock();
        }
        if (task_delay_ms > 500) task_delay_ms = 500;
        else if (task_delay_ms < 1) task_delay_ms = 1;
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */
void sys_display_init(void)
{
    ESP_LOGI(TAG, "Initialize SPI bus (QSPI)");
    const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        PIN_LCD_PCLK, PIN_LCD_DATA0, PIN_LCD_DATA1, PIN_LCD_DATA2, PIN_LCD_DATA3,
        LCD_H_RES * LCD_V_RES * LCD_BIT_PER_PIXEL / 8);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config =
        SH8601_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, notify_lvgl_flush_ready, &disp_drv);

    sh8601_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = { .use_qspi_interface = 1 },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                             &io_config, &io_handle));

    ESP_LOGI(TAG, "Install SH8601 panel driver");
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

#if PIN_BK_LIGHT >= 0
    gpio_set_direction(PIN_BK_LIGHT, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_BK_LIGHT, 1);
#endif

    ESP_LOGI(TAG, "Initialize LVGL");
    lv_init();

    lv_color_t *buf1 = heap_caps_malloc(LCD_H_RES * LVGL_BUF_HEIGHT * sizeof(lv_color_t),
                                        MALLOC_CAP_DMA);
    assert(buf1);
    lv_color_t *buf2 = heap_caps_malloc(LCD_H_RES * LVGL_BUF_HEIGHT * sizeof(lv_color_t),
                                        MALLOC_CAP_DMA);
    assert(buf2);
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, LCD_H_RES * LVGL_BUF_HEIGHT);

    ESP_LOGI(TAG, "Register display driver");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.rounder_cb = lvgl_rounder_cb;
    disp_drv.drv_update_cb = lvgl_update_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
    lv_disp_drv_register(&disp_drv);

    ESP_LOGI(TAG, "Install LVGL tick timer");
    const esp_timer_create_args_t tick_timer_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick"};
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_MS * 1000));

    lvgl_mux = xSemaphoreCreateMutex();
    assert(lvgl_mux);
    xTaskCreate(lvgl_port_task, "LVGL", LVGL_TASK_STACK, NULL, LVGL_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "Display ready");
}

lv_disp_t *sys_display_get_disp(void)
{
    return lv_disp_get_default();
}

SemaphoreHandle_t sys_display_get_mutex(void)
{
    return lvgl_mux;
}

bool sys_display_lock(int timeout_ms)
{
    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mux, timeout_ticks) == pdTRUE;
}

void sys_display_unlock(void)
{
    xSemaphoreGive(lvgl_mux);
}

void sys_display_set_gesture_cb(sys_gesture_cb_t cb)
{
    gesture_cb = cb;
}
