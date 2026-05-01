#include "sys_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "sys_cfg";
#define NVS_NS  "sys_cfg"

static nvs_handle_t nvs_h = 0;
static bool initialized = false;

static const char DEFAULT_SSID[]  = "BABY&L";
static const char DEFAULT_PASS[]  = "LiangBaby123.";

void sys_config_init(void)
{
    esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &nvs_h);
    if (ret == ESP_OK) {
        initialized = true;
        ESP_LOGI(TAG, "NVS ready");
    } else {
        ESP_LOGE(TAG, "NVS open failed: %d", ret);
    }
}

static bool _get_str(const char *key, char *buf, size_t len, const char *def)
{
    if (!initialized) { strncpy(buf, def, len); return true; }
    size_t slen = len;
    esp_err_t ret = nvs_get_str(nvs_h, key, buf, &slen);
    if (ret != ESP_OK) { strncpy(buf, def, len); }
    return ret == ESP_OK;
}

static void _set_str(const char *key, const char *val)
{
    if (initialized) {
        nvs_set_str(nvs_h, key, val);
        nvs_commit(nvs_h);
    }
}

static void _get_u8(const char *key, uint8_t *out, uint8_t def)
{
    if (!initialized) { *out = def; return; }
    uint8_t val;
    if (nvs_get_u8(nvs_h, key, &val) == ESP_OK) *out = val; else *out = def;
}

static void _set_u8(const char *key, uint8_t val)
{
    if (initialized) {
        nvs_set_u8(nvs_h, key, val);
        nvs_commit(nvs_h);
    }
}

static void _get_u32(const char *key, uint32_t *out, uint32_t def)
{
    if (!initialized) { *out = def; return; }
    uint32_t val;
    if (nvs_get_u32(nvs_h, key, &val) == ESP_OK) *out = val; else *out = def;
}

/* WiFi */
bool sys_config_get_wifi_ssid(char *buf, size_t len) { return _get_str("wifi_ssid", buf, len, DEFAULT_SSID); }
void sys_config_set_wifi_ssid(const char *ssid)      { _set_str("wifi_ssid", ssid); }
bool sys_config_get_wifi_pass(char *buf, size_t len) { return _get_str("wifi_pass", buf, len, DEFAULT_PASS); }
void sys_config_set_wifi_pass(const char *pass)      { _set_str("wifi_pass", pass); }

/* API keys */
void sys_config_set_llm_key(const char *key)         { _set_str("llm_key", key); }
bool sys_config_get_llm_key(char *buf, size_t len)   { return _get_str("llm_key", buf, len, ""); }
void sys_config_set_weather_key(const char *key)     { _set_str("weather_key", key); }
bool sys_config_get_weather_key(char *buf, size_t len){ return _get_str("weather_key", buf, len, ""); }

/* Device settings */
uint8_t  sys_config_get_brightness(void)             { uint8_t v=255; _get_u8("brightness", &v, 255); return v; }
void     sys_config_set_brightness(uint8_t val)      { _set_u8("brightness", val); }
uint32_t sys_config_get_step_goal(void)              { uint32_t v=10000; _get_u32("step_goal", &v, 10000); return v; }
void     sys_config_set_step_goal(uint32_t val)      { if (initialized) { nvs_set_u32(nvs_h, "step_goal", val); nvs_commit(nvs_h); } }
