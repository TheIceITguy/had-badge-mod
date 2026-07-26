/* QMC5883L magnetometer on the SAO I2C bus at 0x0D.
 *
 * An alternative field source to the AK09916 inside the ICM-20948, which on this
 * badge reads with 200 to 400 uT of spread against an earth field of 25 to 65 uT.
 * The reason to reach for this part specifically is OSR: it filters inside the
 * sensor, before the ADC samples, which is where a disturbance faster than the
 * sample rate has to be caught. The AK09916 has no equivalent control.
 *
 * Transport only, like drivers/imu.h: counts to microtesla here, and the compass
 * service owns the fusion, calibration and smoothing.
 *
 * These boards are sold as HMC5883L and are not. Honeywell's part is discontinued
 * and lives at 0x1E with an unrelated register map, so a board whose chip is marked
 * DA5883 is this part instead. See
 * docs/src/content/docs/hardware/magnetometer-options.md. */
#ifndef DRIVERS_QMC5883L_H
#define DRIVERS_QMC5883L_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Full-scale range. The badge default is 8 G, not the 2 G every generic example
 * uses: 2 G is 200 uT full scale, and the interference measured on this badge is
 * 200 to 400 uT, so a 2 G range clips on the noise alone and the result looks like
 * a broken sensor. 8 G costs resolution (3000 against 12000 LSB per gauss) and
 * still leaves about 1500 counts for a 50 uT field. */
typedef enum { QMC_RANGE_2G, QMC_RANGE_8G } qmc_range_t;

/* Oversampling: the internal filter's bandwidth. More samples is a narrower
 * filter and less in-band noise, at the cost of current. 512 is the reason this
 * part is here, so it is the default. */
typedef enum { QMC_OSR_512, QMC_OSR_256, QMC_OSR_128, QMC_OSR_64 } qmc_osr_t;

/* Output rate. 100 Hz feeds a 20 Hz consumer with room to spare; the slower rates
 * are worth trying against interference, because the spread on this badge changes
 * with sample rate. */
typedef enum { QMC_ODR_10HZ, QMC_ODR_50HZ, QMC_ODR_100HZ, QMC_ODR_200HZ } qmc_odr_t;

typedef struct {
    bool present;        /* the address acknowledged and the chip id matched */
    uint8_t chip_id;     /* raw register 0x0D; 0xFF is the QMC5883L */
    uint32_t reads;      /* samples returned */
    uint32_t errors;     /* failed I2C transactions */
    uint32_t not_ready;  /* polls that found no fresh sample */
    uint32_t overflows;  /* OVL set: the field exceeded the selected range */
    uint32_t skipped;    /* DOR set: a sample was produced and never collected */
} qmc5883l_status_t;

/* Bring up the part on an already-shared bus (see drivers/i2c_bus.h), leaving it
 * in continuous mode. Returns ESP_ERR_NOT_FOUND when nothing acknowledges at 0x0D,
 * so a badge without the module fitted logs one line and carries on. */
esp_err_t qmc5883l_init(int sda_pin, int scl_pin,
                        qmc_range_t range, qmc_osr_t osr, qmc_odr_t odr);

/* One sample in microtesla, sensor frame. False when no fresh sample was ready or
 * the transfer failed, leaving *mx, *my, *mz untouched. */
bool qmc5883l_read(double *mx, double *my, double *mz);

void qmc5883l_get_status(qmc5883l_status_t *out);

#endif /* DRIVERS_QMC5883L_H */
