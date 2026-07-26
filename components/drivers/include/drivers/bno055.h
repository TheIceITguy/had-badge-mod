/* BNO055 absolute orientation sensor on the SAO I2C bus at 0x28 or 0x29.
 *
 * Unlike every other part in drivers/, this one is not a sensor the badge fuses.
 * It carries an accelerometer, a gyroscope, a BMM150 magnetometer and a Cortex-M0
 * running Bosch's BSX3.0 fusion, and it reports a finished heading. That is the
 * reason it is here: its magnetometer sees the same interference the AK09916 does,
 * but fused heading uses the magnetometer only as a slow drift reference while the
 * gyroscope carries short-term motion, so a noisy field still yields a stable
 * bearing. The badge's own stack derives an instantaneous heading from an
 * instantaneous field, which is why 300 uT of noise lands straight in its output.
 *
 * It also settles an open question about the badge. The per-sensor calibration
 * status below is an independent verdict on the environment: a magnetometer that
 * will not reach calibration while sitting on the badge says so from a different
 * vendor's part and a different algorithm.
 *
 * See docs/src/content/docs/hardware/magnetometer-options.md. */
#ifndef DRIVERS_BNO055_H
#define DRIVERS_BNO055_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Per-sensor calibration, 0 to 3 each, 3 being fully calibrated. Read these before
 * trusting a heading: the part reports a confident-looking bearing from the moment
 * it powers up, long before its magnetometer has seen enough to mean anything. */
typedef struct {
    uint8_t sys, gyro, accel, mag;
} bno055_calib_t;

typedef struct {
    bool present;        /* CHIP_ID matched */
    uint8_t chip_id;     /* raw register 0x00; 0xA0 is a BNO055 */
    uint8_t sys_status;  /* 5 = fusion running */
    uint8_t sys_err;     /* non-zero is a fault the part is reporting about itself */
    bool ext_crystal;    /* the external 32.768 kHz crystal was selected */
    bno055_calib_t calib;
    uint32_t reads, errors;
} bno055_status_t;

typedef struct {
    double heading_deg;      /* magnetic heading, 0..360, fused */
    double roll_deg, pitch_deg;
    double mx, my, mz;       /* raw field in uT, for diagnosing interference */
    bno055_calib_t calib;
} bno055_sample_t;

/* Bring it up in NDOF (nine-degree-of-freedom fusion) on an already-shared bus,
 * selecting the external crystal. Returns ESP_ERR_NOT_FOUND when nothing answers
 * with a BNO055 chip id at either address. */
esp_err_t bno055_init(int sda_pin, int scl_pin, bool addr_hi);

/* One fused sample. False on a transport error, leaving *out untouched. */
bool bno055_read(bno055_sample_t *out);

void bno055_get_status(bno055_status_t *out);

#endif /* DRIVERS_BNO055_H */
