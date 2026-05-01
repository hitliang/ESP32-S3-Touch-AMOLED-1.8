#include "sys_i2c.h"
#include "esp_log.h"
#include "esp_io_expander_tca9554.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "sys_i2c";
static SemaphoreHandle_t i2c_mux = NULL;

#define I2C_HOST          I2C_NUM_0
#define I2C_FREQ_HZ       200000
#define PIN_TOUCH_SCL     GPIO_NUM_14
#define PIN_TOUCH_SDA     GPIO_NUM_15

static esp_io_expander_handle_t io_expander = NULL;

void sys_i2c_init(void)
{
    ESP_LOGI(TAG, "Initialize I2C bus");
    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_TOUCH_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = PIN_TOUCH_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_HOST, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_HOST, i2c_conf.mode, 0, 0, 0));

    ESP_LOGI(TAG, "Initialize TCA9554 IO expander");
    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(I2C_HOST,
        ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander));

    esp_io_expander_set_dir(io_expander,
        IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2,
        IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0, 0);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_1, 0);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_2, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0, 1);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_1, 1);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_2, 1);

    i2c_mux = xSemaphoreCreateMutex();
    ESP_LOGI(TAG, "I2C bus ready");
}

i2c_port_t sys_i2c_get_port(void)
{
    return I2C_HOST;
}

esp_io_expander_handle_t sys_i2c_get_io_expander(void)
{
    return io_expander;
}

bool sys_i2c_take(int timeout_ms)
{
    return xSemaphoreTake(i2c_mux, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void sys_i2c_give(void)
{
    xSemaphoreGive(i2c_mux);
}
