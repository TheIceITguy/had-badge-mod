/* Battery service: registers the battery sense settings and brings up the ADC
 * when one is configured.
 *
 * Off by default because the stock badge has no battery sense circuit at all: the
 * upstream schematic routes VBAT to the charger and the regulator and never to an
 * MCU pin, so reading the pack needs a divider added by hand. These settings exist
 * so that modification is a configuration change rather than a firmware edit. */
#include "services/services.h"
#include "drivers/battery.h"

static const setting_t BAT_SCHEMA[] = {
    {.key = "bat_enabled", .type = SET_BOOL, .def = "false",
     .label = "Battery sense", .group = "Battery"},
    /* GPIO11 (the J6 IO11 pad) is the only ADC-capable pin left free on this board,
     * and it sits on ADC2, which the ESP32-S3 cannot read while WiFi is running.
     * Reads then fail, the state reports absent, and the sidebar icon disappears
     * until WiFi stops, which is the honest answer for a value nothing can measure. */
    {.key = "bat_pin", .type = SET_INT, .def = "11", .label = "Sense GPIO (ADC)",
     .group = "Battery", .minv = 0, .maxv = 48, .has_min = true, .has_max = true},
    /* Divider ratio times 100: a 100k/100k pair halves the pack voltage, so 200. */
    {.key = "bat_div_x100", .type = SET_INT, .def = "200",
     .label = "Divider ratio x100", .group = "Battery",
     .minv = 100, .maxv = 1000, .has_min = true, .has_max = true},
};

void battery_svc_init(settings_t *reg)
{
    settings_register_many(reg, BAT_SCHEMA, (int)(sizeof BAT_SCHEMA / sizeof BAT_SCHEMA[0]));
    if (!settings_get_bool(reg, "bat_enabled")) {
        battery_init(-1, 1, 1);   /* logs one line, and every read reports absent */
        return;
    }
    battery_init((int)settings_get_int(reg, "bat_pin"),
                 (int)settings_get_int(reg, "bat_div_x100"), 100);
}
