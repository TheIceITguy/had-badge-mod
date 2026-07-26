/* Shared I2C master bus handles, one per port.
 *
 * Two drivers can sit on the same pair of pins: the SAO header carries the
 * ICM-20948 and can carry a separate magnetometer alongside it. i2c_new_master_bus
 * fails on the second call for a port, so whichever driver initialised first would
 * otherwise own the bus and the other could never join. The handle is owned here
 * instead, and every driver asks for it. */
#ifndef DRIVERS_I2C_BUS_H
#define DRIVERS_I2C_BUS_H

#include "driver/i2c_master.h"
#include "esp_err.h"

/* Bus handle for a port, created on first use with these pins and cached. Later
 * callers get the same handle and their pin arguments are ignored, so a caller
 * asking for different pins on a port that is already up gets a warning and the
 * existing bus: two settings that disagree about where a bus lives is a
 * configuration mistake, and silently rewiring one of them would hide it. */
esp_err_t i2c_bus_get(int port, int sda_pin, int scl_pin, i2c_master_bus_handle_t *out);

#endif /* DRIVERS_I2C_BUS_H */
