/* Notification LED on a spare GPIO (D1 on the stock badge, active low). Optional;
 * the pin stays free for something else when the feature is off.
 *
 * Deliberately dumb: this turns the LED on and off and nothing more. Blink
 * patterns are policy, so they live in the LED service, the way the compass
 * service owns the heading policy and the IMU driver only moves bytes. */
#ifndef DRIVERS_LED_H
#define DRIVERS_LED_H

#include <stdbool.h>
#include "esp_err.h"

/* active_low matches the stock D1, which the GPIO sinks rather than sources. */
esp_err_t led_init(int pin, bool active_low);

void led_set(bool on);

/* True when a LED is configured, so a caller can skip deciding. */
bool led_available(void);

#endif /* DRIVERS_LED_H */
