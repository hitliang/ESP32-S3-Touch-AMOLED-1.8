/*
 * tca9554.h - TCA9554 GPIO 扩展器驱动
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t tca9554_init(void);
esp_err_t tca9554_set_pin(uint8_t pin, bool level);
esp_err_t tca9554_get_pin(uint8_t pin, bool *level);
esp_err_t tca9554_write_all(uint8_t value);
esp_err_t tca9554_read_all(uint8_t *value);

#ifdef __cplusplus
}
#endif
