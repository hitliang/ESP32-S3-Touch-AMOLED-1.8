#include "sys_touch.h"
#include "sys_i2c.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"

static const char *TAG = "sys_touch";

#define TOUCH_HOST        I2C_NUM_0
#define PIN_TOUCH_INT     GPIO_NUM_21
#define PIN_TOUCH_RST     (-1)
#define LCD_H_RES         368
#define LCD_V_RES         448

static esp_lcd_touch_handle_t tp = NULL;
static lv_indev_t *touch_indev = NULL;

/* Exposed for swipe detection in sys_display */
int g_debug_touch_x = -1, g_debug_touch_y = -1;
bool g_debug_touch_pressed = false;

/* Simulated release: FT5x06 never reports release, so after holding
   same position for N frames, fake a release to let LVGL process clicks */
#define FAKE_RELEASE_FRAMES  8    /* ~32ms at 4ms indev period */
static int  same_pos_count = 0;
static int  last_tp_x = -1, last_tp_y = -1;
static bool fake_released = false;

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint16_t tp_x, tp_y;
    uint8_t tp_cnt = 0;
    esp_lcd_touch_read_data(tp);
    bool pressed = esp_lcd_touch_get_coordinates(tp, &tp_x, &tp_y, NULL, &tp_cnt, 1);

    g_debug_touch_x = pressed ? tp_x : -1;
    g_debug_touch_y = pressed ? tp_y : -1;
    g_debug_touch_pressed = pressed;

    if (!pressed || tp_cnt == 0) {
        /* Genuine release */
        data->state = LV_INDEV_STATE_RELEASED;
        same_pos_count = 0;
        fake_released = false;
        return;
    }

    /* FT5x06 is always "pressed" — simulate release when position stabilizes */
    if (tp_x == last_tp_x && tp_y == last_tp_y) {
        same_pos_count++;
    } else {
        same_pos_count = 0;
        fake_released = false;
    }
    last_tp_x = tp_x;
    last_tp_y = tp_y;

    data->point.x = tp_x;
    data->point.y = tp_y;

    if (same_pos_count >= FAKE_RELEASE_FRAMES && !fake_released) {
        /* Fake one release frame to let LVGL process click */
        data->state = LV_INDEV_STATE_RELEASED;
        fake_released = true;
        same_pos_count = 0;  /* reset, will go back to pressed next frame */
    } else {
        data->state = LV_INDEV_STATE_PRESSED;
        if (fake_released) {
            /* Next frame after fake release — this is a "new" press */
            fake_released = false;
        }
    }
}

void sys_touch_init(lv_disp_t *disp)
{
    esp_lcd_panel_io_handle_t tp_io = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)TOUCH_HOST,
                                             &tp_io_config, &tp_io));

    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io, &(esp_lcd_touch_config_t){
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = PIN_TOUCH_RST,
        .int_gpio_num = PIN_TOUCH_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    }, &tp));

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = lvgl_touch_cb;
    indev_drv.user_data = tp;
    touch_indev = lv_indev_drv_register(&indev_drv);

    ESP_LOGI(TAG, "Touch ready");
}

esp_lcd_touch_handle_t sys_touch_get_handle(void)
{
    return tp;
}

void sys_touch_set_enabled(bool enabled)
{
    if (touch_indev) {
        lv_indev_enable(touch_indev, enabled);
    }
}
