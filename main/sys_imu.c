#include "sys_imu.h"
#include "sys_i2c.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include <string.h>

static const char *TAG = "sys_imu";

/* Register map */
#define QMI8658_ADDR          0x6B
#define REG_WHOAMI            0x00
#define REG_CTRL1             0x02
#define REG_CTRL2             0x03
#define REG_CTRL3             0x04
#define REG_CTRL5             0x06
#define REG_CTRL7             0x08
#define REG_CTRL8             0x09
#define REG_RESET             0x60
#define REG_STATUSINT         0x2D
#define REG_STATUS0           0x2E
#define REG_AX_L              0x35
#define REG_GX_L              0x3B

#define WHOAMI_VAL             0x05

/* Accel: range=1(4G), ODR=3(1000Hz) → CTRL2 = (1<<4)|3 = 0x13 */
/* Gyro:  range=2(64DPS), ODR=3(896.8Hz) → CTRL3 = (2<<4)|3 = 0x23 */
/* CTRL5: LPF accel mode 0, gyro mode 3 → (0<<1)|(3<<5) = 0x60 */
#define CTRL2_VAL  0x13
#define CTRL3_VAL  0x23
#define CTRL5_VAL  0x60
#define CTRL7_VAL  0x03   /* Enable accel + gyro */
#define CTRL8_VAL  0x80   /* STATUSINT.bit7 as CTRL9 handshake */

static bool init_ok = false;
static sys_imu_data_t latest;
static int step_count = 0;

/* Step counting */
static float accel_mag_history[4] = {0};
static int   accel_mag_idx = 0;
static bool  step_armed = false;

static esp_err_t imu_read_reg(uint8_t reg, uint8_t *data, uint8_t len)
{
    if (!sys_i2c_take(50)) return ESP_ERR_TIMEOUT;
    uint8_t r = reg;
    esp_err_t ret = i2c_master_write_read_device(
        sys_i2c_get_port(), QMI8658_ADDR, &r, 1, data, len, pdMS_TO_TICKS(50));
    sys_i2c_give();
    return ret;
}

static esp_err_t imu_write_reg(uint8_t reg, uint8_t val)
{
    if (!sys_i2c_take(50)) return ESP_ERR_TIMEOUT;
    uint8_t buf[2] = {reg, val};
    esp_err_t ret = i2c_master_write_to_device(
        sys_i2c_get_port(), QMI8658_ADDR, buf, 2, pdMS_TO_TICKS(50));
    sys_i2c_give();
    return ret;
}

static void imu_read_task(void *arg)
{
    float smooth_r = 0, smooth_p = 0;

    while (1) {
        uint8_t status;
        if (imu_read_reg(REG_STATUS0, &status, 1) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (!(status & 0x01)) {  /* accel data ready */
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        uint8_t raw[12];
        if (imu_read_reg(REG_AX_L, raw, 12) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int16_t ax = (int16_t)(raw[1] << 8 | raw[0]);
        int16_t ay = (int16_t)(raw[3] << 8 | raw[2]);
        int16_t az = (int16_t)(raw[5] << 8 | raw[4]);
        int16_t gx = (int16_t)(raw[7] << 8 | raw[6]);
        int16_t gy = (int16_t)(raw[9] << 8 | raw[8]);
        int16_t gz = (int16_t)(raw[11]<< 8 | raw[10]);

        /* Convert to physical units */
        latest.accel_x = ax / 8192.0f;    /* 4G = 8192 LSB/G */
        latest.accel_y = ay / 8192.0f;
        latest.accel_z = az / 8192.0f;
        latest.gyro_x  = gx / 32.0f;      /* 64DPS = 32 LSB/dps */
        latest.gyro_y  = gy / 32.0f;
        latest.gyro_z  = gz / 32.0f;

        /* Roll / Pitch. az = -1G when flat (chip Z points up), so negate az */
        float raw_r = atan2f(latest.accel_y, -latest.accel_z) * 57.29578f;
        float raw_p = atan2f(-latest.accel_x, -latest.accel_z) * 57.29578f;

        /* Heavy low-pass filter for smooth display */
        float a = 0.05f;
        smooth_r = smooth_r * (1.0f - a) + raw_r * a;
        smooth_p = smooth_p * (1.0f - a) + raw_p * a;

        if (fabsf(smooth_r) < 0.5f) smooth_r = 0;
        if (fabsf(smooth_p) < 0.5f) smooth_p = 0;

        latest.roll  = smooth_r;
        latest.pitch = smooth_p;

        /* Step counting via magnitude peak detection */
        float mag = sqrtf(latest.accel_x * latest.accel_x +
                          latest.accel_y * latest.accel_y +
                          latest.accel_z * latest.accel_z);
        accel_mag_history[accel_mag_idx] = mag;
        accel_mag_idx = (accel_mag_idx + 1) % 4;

        if (mag > 1.3f && !step_armed) step_armed = true;
        if (mag < 0.9f && step_armed) { step_count++; step_armed = false; }

        vTaskDelay(pdMS_TO_TICKS(10));  /* ~100Hz */
    }
}

void sys_imu_init(void)
{
    /* Reset chip */
    imu_write_reg(REG_RESET, 0xB0);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Check whoami */
    uint8_t id;
    if (imu_read_reg(REG_WHOAMI, &id, 1) != ESP_OK || id != WHOAMI_VAL) {
        ESP_LOGW(TAG, "QMI8658 not found (id=0x%02x)", id);
        init_ok = false;
    } else {
        ESP_LOGI(TAG, "QMI8658 found");

        /* Address auto-increment, Little-Endian */
        imu_write_reg(REG_CTRL1, 0x40);

        /* Configure accel */
        imu_write_reg(REG_CTRL2, CTRL2_VAL);

        /* Configure gyro */
        imu_write_reg(REG_CTRL3, CTRL3_VAL);

        /* Configure LPF */
        imu_write_reg(REG_CTRL5, CTRL5_VAL);

        /* STATUSINT for CTRL9 handshake */
        imu_write_reg(REG_CTRL8, CTRL8_VAL);

        /* Enable accel + gyro */
        imu_write_reg(REG_CTRL7, CTRL7_VAL);

        init_ok = true;
    }

    xTaskCreate(imu_read_task, "IMU", 2048, NULL, 5, NULL);
    ESP_LOGI(TAG, "IMU task started (ok=%d)", init_ok);
}

void sys_imu_get_data(sys_imu_data_t *out)
{
    memcpy(out, &latest, sizeof(latest));
}

int  sys_imu_get_steps(void)    { return step_count; }
void sys_imu_reset_steps(void)  { step_count = 0; }
