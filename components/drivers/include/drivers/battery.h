/* LiPo battery sense via ADC (optional; off when pin < 0). */
#ifndef DRIVERS_BATTERY_H
#define DRIVERS_BATTERY_H

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    bool present;
    int pct;        /* 0..100, -1 if unknown */
    float volts;
    /* Both unreadable on the stock board and always false: the charger's STAT pin
     * drives only the charge LED, and VBUS never reaches an MCU pin. Kept so a
     * badge with those signals modded in has somewhere to report them. */
    bool charging;
    bool usb;
} battery_state_t;

esp_err_t battery_init(int adc_pin, int divider_num, int divider_den);
bool battery_read(battery_state_t *out);   /* returns present */

#endif /* DRIVERS_BATTERY_H */
