#include "sys_button.h"
#include "sys_display.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sys_btn";

#define BTN_PIN    GPIO_NUM_0
#define LONG_MS    3000

static bool screen_on = true;
static int64_t press_time = 0;

static void IRAM_ATTR btn_isr(void *arg)
{
    int64_t now = esp_timer_get_time() / 1000;
    if (gpio_get_level(BTN_PIN) == 0) {
        press_time = now;  /* pressed */
    } else {
        int64_t held = now - press_time;
        press_time = 0;
        if (held < 30) return;  /* debounce */

        if (held >= LONG_MS) {
            ESP_LOGI(TAG, "Long press (%lldms)", held);
        } else {
            ESP_LOGI(TAG, "Short press (%lldms)", held);
            screen_on = !screen_on;
            /* Toggle display via LVGL background opacity */
            if (sys_display_lock(100)) {
                lv_obj_t *scr = lv_scr_act();
                lv_obj_set_style_bg_opa(scr, screen_on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
                sys_display_unlock();
            }
        }
    }
}

void sys_button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BTN_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&cfg);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BTN_PIN, btn_isr, NULL);
    gpio_set_intr_type(BTN_PIN, GPIO_INTR_ANYEDGE);

    ESP_LOGI(TAG, "Button ready (GPIO%d)", BTN_PIN);
}
