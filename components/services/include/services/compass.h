/* Compass service: samples the IMU, applies the saved magnetometer calibration,
 * and publishes one smoothed true heading that the map-style apps read instead
 * of GPS course over ground. Owns the sampling task and the calibration blob. */
#ifndef SERVICES_COMPASS_H
#define SERVICES_COMPASS_H

#include <stdint.h>
#include <stdbool.h>
#include "core/settings.h"
#include "drivers/imu.h"
#include "util/compass.h"

/* Latest fused heading. Copied out whole, like gps_fix_t. */
typedef struct {
    bool valid;           /* heading_deg is fresh and calibrated */
    double heading_deg;   /* smoothed, degrees true clockwise from north (0..360) */
    double magnetic_deg;  /* the same heading before declination, for diagnostics */
    double roll_deg;
    double pitch_deg;
    bool calibrated;      /* a usable calibration is loaded */
} compass_reading_t;

/* Health/activity snapshot for the Compass and Diagnostics pages. The four
 * "why is there no heading" causes need four different user actions, so each one
 * is carried as its own fact rather than left to be guessed from the others. */
typedef struct {
    bool enabled;              /* imu_enabled is on in Settings */
    bool running;              /* sampling task is active (enabled and IMU found) */
    bool present;              /* the IMU answered at boot */
    bool mag_present;          /* the magnetometer answered at boot */
    compass_state_t state;     /* derived once here so every page agrees */
    bool cal_active;           /* a calibration sweep is in progress */
    uint32_t cal_samples;      /* samples folded into the current sweep */
    bool cal_ready;            /* the current sweep is now usable */
    bool cal_stored;           /* a calibration is loaded from NVS */
    bool cal_in_use;           /* ...and mag_cal_use lets it correct the field */
    uint32_t samples;          /* total fused headings since boot */
    uint32_t errors;           /* failed reads since boot */
    uint32_t implausible;      /* samples rejected as not-a-magnetic-field */
    double field_ut;           /* magnitude of the last corrected sample, uT */
    double field_raw_ut;       /* magnitude before the correction, uT */
    bool cal_bad;
    /* Last magnetometer self-test, valid only while selftest_done is set. The
     * result is kept rather than recomputed because the test takes 100 ms and
     * changes the measurement mode, so a page cannot run it on every tick. */
    bool selftest_done;
    imu_mag_selftest_t selftest;              /* raw field is sane, the stored correction is not */
    uint32_t ms_since_sample;  /* since the last fused heading; UINT32_MAX if never */
    uint32_t ms_since_tilt;    /* since the last accelerometer-only attitude */
    uint8_t imu_whoami;        /* raw WHO_AM_I, so a dead part is distinguishable */
    uint32_t imu_reads;        /* driver-level successful reads */
    uint32_t imu_errors;       /* driver-level I2C failures, including mag-only ones */
    double declination_deg;    /* the configured declination, east positive */
    compass_reading_t reading; /* latest heading snapshot */
} compass_status_t;

void compass_svc_init(settings_t *reg);

/* Latest heading. Returns true when it is usable, so callers can write
 * `if (compass_get(&c) && ...)` the way they do with gps_get_fix. */
bool compass_get(compass_reading_t *out);

/* Full status snapshot (always succeeds; running=false when the IMU is off). */
void compass_get_status(compass_status_t *out);

/* Magnetometer calibration. The sweep accumulates in RAM while the user turns
 * the badge through every orientation; saving writes it to NVS and applies it
 * at once, so there is no reboot in the loop. */
/* Ask the sampling task to run the magnetometer self-test. Returns at once; the
 * verdict appears in compass_status_t.selftest about 100 ms later. */
void compass_selftest_begin(void);

void compass_cal_begin(void);
bool compass_cal_save(void);   /* false while the sweep is not usable yet */
void compass_cal_cancel(void);

#endif /* SERVICES_COMPASS_H */
