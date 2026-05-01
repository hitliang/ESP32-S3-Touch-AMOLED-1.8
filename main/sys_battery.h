#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void     sys_battery_init(void);
void     sys_battery_update(void);
uint8_t  sys_battery_get_percent(void);
bool     sys_battery_is_charging(void);
bool     sys_battery_is_present(void);

#ifdef __cplusplus
}
#endif
