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

int g_debug_touch_x = -1, g_debug_touch_y = -1;
bool g_debug_touch_pressed = false;

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint16_t tp_x, tp_y;
    uint8_t tp_cnt = 0;
    esp_lcd_touch_read_data(tp);
    bool tp_pressed = esp_lcd_touch_get_coordinates(tp, &tp_x, &tp_y, NULL, &tp_cnt, 1);

    g_debug_touch_x = tp_pressed ? tp_x : -1;
    g_debug_touch_y = tp_pressed ? tp_y : -1;
    g_debug_touch_pressed = tp_pressed;

    if (tp_pressed && tp_cnt > 0) {
        data->point.x = tp_x;
        data->point.y = tp_y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
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

esp_lcd_touch_handle_t sys_touch_get_handle(void)     { return tp; }
lv_indev_t *sys_touch_get_indev(void)                  { return touch_indev; }

void sys_touch_set_enabled(bool enabled)
{
    if (touch_indev) lv_indev_enable(touch_indev, enabled);
}
