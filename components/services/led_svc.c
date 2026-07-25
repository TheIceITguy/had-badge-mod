/* Notification LED service: turns badge state into blink patterns on D1, the way
 * a BlackBerry used one LED to tell you something was waiting without lighting
 * the screen.
 *
 * One monochrome LED means the vocabulary is timing, not colour, so each state
 * gets a distinguishable rhythm and the highest-priority state wins. A 50 ms
 * periodic timer walks the pattern; the LED driver itself only knows on and off. */
#include "services/services.h"
#include "drivers/led.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "led_svc";

#define TICK_MS      50
#define TEST_MS      1000   /* solid, long enough to see it from the Diagnostics page */

/* on_ms out of period_ms. The unread blink is deliberately brief and slow: it has
 * to be noticeable across a room and cheap enough to leave running for hours. */
#define UNREAD_ON_MS      60
#define UNREAD_PERIOD_MS  3000
#define BEAT_ON_MS        30

static bool s_on_msg = true;
static uint32_t s_beat_period_ms;   /* 0 = heartbeat off */

static volatile bool s_unread;
static volatile uint32_t s_test_left_ms;
static uint32_t s_phase_ms;

static const setting_t LED_SCHEMA[] = {
    {.key = "led_enabled", .type = SET_BOOL, .def = "true",
     .label = "Notification LED", .group = "LED"},
    {.key = "led_pin", .type = SET_INT, .def = "1", .label = "LED GPIO", .group = "LED",
     .minv = 0, .maxv = 48, .has_min = true, .has_max = true},
    {.key = "led_active_lo", .type = SET_BOOL, .def = "true",
     .label = "LED is active low", .group = "LED"},
    {.key = "led_on_msg", .type = SET_BOOL, .def = "true",
     .label = "Blink on unread message", .group = "LED"},
    {.key = "led_beat_s", .type = SET_INT, .def = "0",
     .label = "Idle heartbeat (s, 0=off)", .group = "LED",
     .minv = 0, .maxv = 3600, .has_min = true, .has_max = true},
};

/* Highest priority state that wants the LED. Charging and low battery belong here
 * too, and the event bus already carries them, but this board has no battery sense
 * circuit at all, so those patterns would be unreachable code until the sense pin
 * exists in hardware. */
static bool pattern(uint32_t *on_ms, uint32_t *period_ms)
{
    if (s_test_left_ms) { *on_ms = TEST_MS; *period_ms = TEST_MS; return true; }
    if (s_unread && s_on_msg) { *on_ms = UNREAD_ON_MS; *period_ms = UNREAD_PERIOD_MS; return true; }
    if (s_beat_period_ms) { *on_ms = BEAT_ON_MS; *period_ms = s_beat_period_ms; return true; }
    return false;
}

/* Runs in the esp_timer task and touches only the GPIO, so it stays off the UI
 * task and cannot be delayed by a redraw. */
static void led_tick(void *arg)
{
    (void)arg;

    if (s_test_left_ms)
        s_test_left_ms = s_test_left_ms > TICK_MS ? s_test_left_ms - TICK_MS : 0;

    uint32_t on_ms, period_ms;
    if (!pattern(&on_ms, &period_ms)) {
        s_phase_ms = 0;
        led_set(false);
        return;
    }

    s_phase_ms += TICK_MS;
    if (s_phase_ms >= period_ms) s_phase_ms = 0;
    led_set(s_phase_ms < on_ms);
}

/* Handlers run on the publisher's stack, so they only set a flag. */
static void on_event(eb_event_t ev, const void *payload, void *ctx)
{
    (void)payload;
    (void)ctx;
    if (ev == EV_MESSAGE_RECEIVED) s_unread = true;
    else if (ev == EV_MESSAGES_READ) s_unread = false;
}

void led_svc_test(void) { if (led_available()) s_test_left_ms = TEST_MS; }

void led_svc_init(settings_t *reg, eventbus_t *bus)
{
    settings_register_many(reg, LED_SCHEMA, (int)(sizeof LED_SCHEMA / sizeof LED_SCHEMA[0]));
    if (!settings_get_bool(reg, "led_enabled")) return;

    int pin = (int)settings_get_int(reg, "led_pin");
    if (led_init(pin, settings_get_bool(reg, "led_active_lo")) != ESP_OK) return;

    s_on_msg = settings_get_bool(reg, "led_on_msg");
    s_beat_period_ms = (uint32_t)settings_get_int(reg, "led_beat_s") * 1000u;

    eventbus_subscribe(bus, EV_MESSAGE_RECEIVED, on_event, NULL);
    eventbus_subscribe(bus, EV_MESSAGES_READ, on_event, NULL);

    const esp_timer_create_args_t targs = { .callback = led_tick, .name = "led" };
    esp_timer_handle_t t;
    esp_err_t e = esp_timer_create(&targs, &t);
    if (e != ESP_OK) { ESP_LOGE(TAG, "timer: %s", esp_err_to_name(e)); return; }
    esp_timer_start_periodic(t, TICK_MS * 1000);
}
