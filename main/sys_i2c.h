#pragma once

#include "driver/i2c.h"
#include "esp_io_expander.h"

#ifdef __cplusplus
extern "C" {
#endif

void sys_i2c_init(void);
i2c_port_t sys_i2c_get_port(void);
esp_io_expander_handle_t sys_i2c_get_io_expander(void);

#ifdef __cplusplus
}
#endif
