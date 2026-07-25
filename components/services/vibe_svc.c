/* Vibration service: registers the motor settings, starts the driver when it is
 * enabled, and buzzes on the events worth feeling with the badge in a pocket. */
#include "services/services.h"
#include "drivers/vibe.h"
#include "esp_log.h"

static const char *TAG = "vibe_svc";

static int s_ms = 180;
static bool s_on_msg = true;

static const setting_t VIBE_SCHEMA[] = {
    {.key = "vibe_enabled", .type = SET_BOOL, .def = "true",
     .label = "Vibration motor", .group = "Vibration"},
    {.key = "vibe_pin", .type = SET_INT, .def = "12", .label = "Motor GPIO",
     .group = "Vibration", .minv = 0, .maxv = 48, .has_min = true, .has_max = true},
    {.key = "vibe_ms", .type = SET_INT, .def = "180", .label = "Buzz length (ms)",
     .group = "Vibration", .minv = 20, .maxv = VIBE_MAX_MS, .has_min = true, .has_max = true},
    {.key = "vibe_on_msg", .type = SET_BOOL, .def = "true",
     .label = "Buzz on message", .group = "Vibration"},
};

/* Handlers run on the publisher's stack, which for an incoming message is the
 * radio RX path, so this does nothing but arm the driver's timer. */
static void on_event(eb_event_t ev, const void *payload, void *ctx)
{
    (void)payload;
    (void)ctx;
    if (ev == EV_MESSAGE_RECEIVED && s_on_msg) vibe_pulse(s_ms);
}

/* The motor pin is a bare GPIO with no header of its own, so it is usually a pad
 * borrowed from something else. Name the clash in the log rather than refusing:
 * only the user knows what is actually soldered where, and a silent GPS or a
 * dead motor is otherwise a long afternoon. */
static void warn_if_shared(settings_t *reg, int pin)
{
    if (settings_get_bool(reg, "gps_enabled") &&
        (pin == (int)settings_get_int(reg, "gps_rx_pin") ||
         pin == (int)settings_get_int(reg, "gps_tx_pin")))
        ESP_LOGW(TAG, "GPIO%d is also a GPS UART pin; move one of them", pin);

    if (settings_get_bool(reg, "imu_enabled") &&
        (pin == (int)settings_get_int(reg, "imu_sda_pin") ||
         pin == (int)settings_get_int(reg, "imu_scl_pin")))
        ESP_LOGW(TAG, "GPIO%d is also a compass I2C pin; move one of them", pin);
}

/* Buzz once regardless of vibe_on_msg: the point is to test the motor, and
 * someone who turned message buzzing off still wants to know the wiring works. */
void vibe_svc_test(void) { vibe_pulse(s_ms); }

void vibe_svc_init(settings_t *reg, eventbus_t *bus)
{
    settings_register_many(reg, VIBE_SCHEMA, (int)(sizeof VIBE_SCHEMA / sizeof VIBE_SCHEMA[0]));
    if (!settings_get_bool(reg, "vibe_enabled")) return;

    int pin = (int)settings_get_int(reg, "vibe_pin");
    warn_if_shared(reg, pin);
    if (vibe_init(pin) != ESP_OK) return;

    s_ms = (int)settings_get_int(reg, "vibe_ms");
    s_on_msg = settings_get_bool(reg, "vibe_on_msg");
    eventbus_subscribe(bus, EV_MESSAGE_RECEIVED, on_event, NULL);
}
