/* Vibration motor on a spare GPIO: driving the pin high vibrates. Optional;
 * only started when enabled, so the pin stays free for anything else when it is
 * not. Pulses are one-shot and timed by esp_timer, so no caller ever blocks
 * waiting for a buzz to end. */
#ifndef DRIVERS_VIBE_H
#define DRIVERS_VIBE_H

#include <stdbool.h>
#include "esp_err.h"

/* Longest single pulse. A motor left on is a flat battery and a hot driver, so
 * a caller cannot ask for more than this however it got its number. */
#define VIBE_MAX_MS 2000

/* Claim the pin and leave the motor off. */
esp_err_t vibe_init(int pin);

/* Buzz for ms milliseconds, then stop on a timer. Safe to call from an event
 * bus handler (it only touches a GPIO and arms a timer, never LVGL and never
 * blocking). A pulse arriving while one is running restarts it rather than
 * stacking, so a burst of messages is one buzz and not a rattle. No-op when no
 * motor is configured. */
void vibe_pulse(int ms);

/* True when a motor is configured, so a caller can skip the work of deciding. */
bool vibe_available(void);

#endif /* DRIVERS_VIBE_H */
