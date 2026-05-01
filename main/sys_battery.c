#include "sys_battery.h"
#include "sys_i2c.h"
#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "sys_batt";

#define AXP2101_ADDR      0x34
#define AXP2101_BAT_REG   0xA4

static uint8_t  batt_pct      = 0;
static bool     batt_present  = false;
static bool     batt_charging = false;

static esp_err_t batt_read_reg(uint8_t reg, uint8_t *val)
{
    if (!val) return ESP_ERR_INVALID_ARG;
    if (!sys_i2c_take(50)) return ESP_ERR_TIMEOUT;
    uint8_t r = reg;
    esp_err_t ret = i2c_master_write_read_device(
        sys_i2c_get_port(), AXP2101_ADDR,
        &r, 1, val, 1, pdMS_TO_TICKS(50));
    sys_i2c_give();
    return ret;
}

void sys_battery_init(void)
{
    uint8_t data = 0;
    esp_err_t ret = batt_read_reg(AXP2101_BAT_REG, &data);
    if (ret == ESP_OK) {
        batt_pct     = (data <= 100) ? data : 100;
        batt_present = true;
        ESP_LOGI(TAG, "Found, battery %d%%", batt_pct);
    } else {
        ESP_LOGW(TAG, "Not found (err=0x%x)", ret);
    }
}

void sys_battery_update(void)
{
    if (!batt_present) return;
    uint8_t data = 0;
    if (batt_read_reg(AXP2101_BAT_REG, &data) == ESP_OK) {
        batt_pct = (data <= 100) ? data : 100;
    }
}

uint8_t sys_battery_get_percent(void)  { return batt_pct; }
bool    sys_battery_is_charging(void)  { return batt_charging; }
bool    sys_battery_is_present(void)   { return batt_present; }
