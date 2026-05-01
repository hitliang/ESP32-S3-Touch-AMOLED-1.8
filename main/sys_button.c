#include "sys_button.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "sys_btn";

#define BTN_PIN           GPIO_NUM_0
#define DEBOUNCE_MS       50
#define LONG_PRESS_MS     3000

static bool     btn_was_down = false;
static int64_t  btn_down_time = 0;

void sys_button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BTN_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&cfg);
    ESP_LOGI(TAG, "Button ready (GPIO%d)", BTN_PIN);
}

bool sys_button_poll(void)
{
    /* Poll once per ~50ms for debounce */
    bool is_down = (gpio_get_level(BTN_PIN) == 0);
    bool detected = false;

    if (is_down && !btn_was_down) {
        btn_down_time = esp_timer_get_time() / 1000;
    }
    btn_was_down = is_down;

    if (!is_down && btn_down_time > 0) {
        int64_t held = esp_timer_get_time() / 1000 - btn_down_time;
        btn_down_time = 0;
        if (held > DEBOUNCE_MS) {
            if (held >= LONG_PRESS_MS) {
                ESP_LOGI(TAG, "Long press (%lldms)", held);
            } else {
                ESP_LOGI(TAG, "Short press (%lldms)", held);
                detected = true;
            }
        }
    }

    return detected;
}
