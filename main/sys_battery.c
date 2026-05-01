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

void sys_battery_init(void)
{
    /* Probe AXP2101 */
    uint8_t data = 0;
    esp_err_t ret = i2c_master_write_read_device(
        sys_i2c_get_port(), AXP2101_ADDR,
        &(uint8_t){AXP2101_BAT_REG}, 1, &data, 1,
        pdMS_TO_TICKS(100));

    if (ret == ESP_OK) {
        batt_pct     = (data <= 100) ? data : 100;
        batt_present = true;
        ESP_LOGI(TAG, "Found, battery %d%%", batt_pct);
    } else {
        batt_present = false;
        ESP_LOGW(TAG, "Not found (err=0x%x)", ret);
    }
}

void sys_battery_update(void)
{
    if (!batt_present) return;

    uint8_t data = 0;
    esp_err_t ret = i2c_master_write_read_device(
        sys_i2c_get_port(), AXP2101_ADDR,
        &(uint8_t){AXP2101_BAT_REG}, 1, &data, 1,
        pdMS_TO_TICKS(50));

    if (ret == ESP_OK) {
        batt_pct = (data <= 100) ? data : 100;
    }
}

uint8_t sys_battery_get_percent(void)  { return batt_pct; }
bool    sys_battery_is_charging(void)  { return batt_charging; }
bool    sys_battery_is_present(void)   { return batt_present; }
