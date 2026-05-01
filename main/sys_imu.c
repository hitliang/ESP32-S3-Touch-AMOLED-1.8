#include "sys_imu.h"
#include "sys_i2c.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include <string.h>

static const char *TAG = "sys_imu";

#define QMI8658_ADDR          0x6B
#define QMI8658_REG_WHOAMI    0x00
#define QMI8658_WHOAMI_VAL    0x05
#define QMI8658_REG_CTRL1     0x02
#define QMI8658_REG_CTRL2     0x03
#define QMI8658_REG_CTRL7     0x08
#define QMI8658_REG_STATUS    0x2E
#define QMI8658_REG_TEMP      0x33
#define QMI8658_REG_AX_L      0x35
#define QMI8658_REG_GX_L      0x3B

#define QMI8658_CTRL7_ENABLE  0x03

static bool init_ok = false;
static sys_imu_data_t latest;
static int step_count = 0;

/* Peak detection state for step counting */
static float accel_mag_history[4] = {0};
static int   accel_mag_idx = 0;
static float accel_mag_avg = 0;
static bool  step_armed = false;

static esp_err_t imu_read_reg(uint8_t reg, uint8_t *data, uint8_t len)
{
    return i2c_master_write_read_device(
        sys_i2c_get_port(), QMI8658_ADDR,
        &reg, 1, data, len, pdMS_TO_TICKS(50));
}

static esp_err_t imu_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_write_to_device(
        sys_i2c_get_port(), QMI8658_ADDR,
        buf, 2, pdMS_TO_TICKS(50));
}

static void imu_read_task(void *arg)
{
    while (1) {
        uint8_t status;
        if (imu_read_reg(QMI8658_REG_STATUS, &status, 1) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (!(status & 0x01)) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        uint8_t raw[12];
        if (imu_read_reg(QMI8658_REG_AX_L, raw, 12) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int16_t ax = (int16_t)(raw[1] << 8 | raw[0]);
        int16_t ay = (int16_t)(raw[3] << 8 | raw[2]);
        int16_t az = (int16_t)(raw[5] << 8 | raw[4]);
        int16_t gx = (int16_t)(raw[7] << 8 | raw[6]);
        int16_t gy = (int16_t)(raw[9] << 8 | raw[8]);
        int16_t gz = (int16_t)(raw[11]<< 8 | raw[10]);

        latest.accel_x = ax / 8192.0f;   /* +/-4G = 8192 LSB/G */
        latest.accel_y = ay / 8192.0f;
        latest.accel_z = az / 8192.0f;
        latest.gyro_x  = gx / 32.0f;     /* +/-64DPS = 32 LSB/dps */
        latest.gyro_y  = gy / 32.0f;
        latest.gyro_z  = gz / 32.0f;

        /* Roll / Pitch from accelerometer */
        latest.roll  = atan2f(latest.accel_y, sqrtf(latest.accel_x * latest.accel_x +
                                                     latest.accel_z * latest.accel_z)) * 57.29578f;
        latest.pitch = atan2f(-latest.accel_x, sqrtf(latest.accel_y * latest.accel_y +
                                                      latest.accel_z * latest.accel_z)) * 57.29578f;

        /* Simple step counting via peak detection on accel magnitude */
        float mag = sqrtf(latest.accel_x * latest.accel_x +
                          latest.accel_y * latest.accel_y +
                          latest.accel_z * latest.accel_z);
        accel_mag_history[accel_mag_idx] = mag;
        accel_mag_idx = (accel_mag_idx + 1) % 4;
        accel_mag_avg = (accel_mag_history[0] + accel_mag_history[1] +
                         accel_mag_history[2] + accel_mag_history[3]) / 4.0f;

        if (mag > 1.3f && !step_armed) step_armed = true;
        if (mag < 0.9f && step_armed) { step_count++; step_armed = false; }

        vTaskDelay(pdMS_TO_TICKS(16));  /* ~62.5Hz */
    }
}

void sys_imu_init(void)
{
    uint8_t whoami;
    if (imu_read_reg(QMI8658_REG_WHOAMI, &whoami, 1) != ESP_OK || whoami != QMI8658_WHOAMI_VAL) {
        ESP_LOGW(TAG, "QMI8658 not found (whoami=0x%02x), using simulated data", whoami);
        init_ok = false;
    } else {
        ESP_LOGI(TAG, "QMI8658 found");

        /* Soft reset via CTRL1, bit 7 */
        imu_write_reg(QMI8658_REG_CTRL1, 0x80);
        vTaskDelay(pdMS_TO_TICKS(10));

        /* CTRL2: accel 4G, gyro 64DPS */
        imu_write_reg(QMI8658_REG_CTRL2, 0x43);

        /* CTRL7: enable accel + gyro */
        imu_write_reg(QMI8658_REG_CTRL7, QMI8658_CTRL7_ENABLE);

        init_ok = true;
    }

    xTaskCreate(imu_read_task, "IMU", 2048, NULL, 5, NULL);
    ESP_LOGI(TAG, "IMU task started");
}

void sys_imu_get_data(sys_imu_data_t *out)
{
    memcpy(out, &latest, sizeof(latest));
}

int  sys_imu_get_steps(void)       { return step_count; }
void sys_imu_reset_steps(void)     { step_count = 0; }
