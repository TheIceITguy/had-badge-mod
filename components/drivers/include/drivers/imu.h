/* ICM-20948 9-axis IMU on the SAO header (I2C unit 1). Optional; only started
 * when enabled. Transport only: this converts counts to physical units, and the
 * compass service does the fusion, calibration and smoothing. */
#ifndef DRIVERS_IMU_H
#define DRIVERS_IMU_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* One sample set in physical units, sensor frame. The magnetometer sits on a
 * second die with its own axes; imu_read() has already rotated it into the
 * accelerometer frame so the two can be fused directly. */
typedef struct {
    double ax, ay, az;   /* accelerometer, g */
    double gx, gy, gz;   /* gyroscope, degrees per second */
    double mx, my, mz;   /* magnetometer, uT; zero when mag_ok is false */
    bool mag_ok;         /* the magnetometer had fresh data this read */
    double temp_c;       /* die temperature */
} imu_sample_t;

/* Health snapshot for diagnostics. Distinguishes "not fitted" from "fitted but
 * the magnetometer is not answering" without the UI having to guess. */
typedef struct {
    bool present;      /* WHO_AM_I matched at init */
    bool mag_present;  /* AK09916 identified itself at init */
    uint8_t whoami;    /* raw WHO_AM_I byte, 0 if the read failed */
    uint32_t reads;    /* successful sample reads */
    uint32_t errors;   /* failed I2C transactions */
} imu_status_t;

/* Bring up the bus and the sensor. addr is IMU_I2C_ADDR, or 0x69 with AD0 tied
 * high. Returns ESP_ERR_NOT_FOUND when nothing answers, so a badge with no IMU
 * fitted logs one line and boots normally. */
esp_err_t imu_init(int sda_pin, int scl_pin, int addr);

/* Read one sample set. Blocking, roughly a millisecond of I2C. Returns false on
 * a transport error, leaving *out untouched. */
bool imu_read(imu_sample_t *out);

/* Status snapshot (always succeeds; present=false when no IMU was found). */
void imu_get_status(imu_status_t *out);

#endif /* DRIVERS_IMU_H */
