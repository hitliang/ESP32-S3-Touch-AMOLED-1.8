#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void sys_config_init(void);

bool     sys_config_get_wifi_ssid(char *buf, size_t len);
void     sys_config_set_wifi_ssid(const char *ssid);
bool     sys_config_get_wifi_pass(char *buf, size_t len);
void     sys_config_set_wifi_pass(const char *pass);

void     sys_config_set_llm_key(const char *key);
bool     sys_config_get_llm_key(char *buf, size_t len);
void     sys_config_set_weather_key(const char *key);
bool     sys_config_get_weather_key(char *buf, size_t len);

uint8_t  sys_config_get_brightness(void);
void     sys_config_set_brightness(uint8_t val);
uint32_t sys_config_get_step_goal(void);
void     sys_config_set_step_goal(uint32_t val);

#ifdef __cplusplus
}
#endif
