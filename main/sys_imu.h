#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float accel_x, accel_y, accel_z;   /* G */
    float gyro_x, gyro_y, gyro_z;      /* dps */
    float roll, pitch;                 /* degrees */
} sys_imu_data_t;

void sys_imu_init(void);
void sys_imu_get_data(sys_imu_data_t *out);
int  sys_imu_get_steps(void);
void sys_imu_reset_steps(void);

#ifdef __cplusplus
}
#endif
