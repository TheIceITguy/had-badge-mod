/* Long-lived background services. Each _init wires a driver/feature to the
 * event bus, settings, and (for status) the UI sidebar. */
#ifndef SERVICES_SERVICES_H
#define SERVICES_SERVICES_H

#include "core/settings.h"
#include "core/eventbus.h"

void battery_svc_init(settings_t *reg);   /* battery sense pin and divider */
void gps_svc_init(settings_t *reg);
void compass_svc_init(settings_t *reg);  /* IMU heading, replaces GPS course */
void time_svc_init(void);
void mesh_svc_init(settings_t *reg);
void mesh_svc_announce_now(void);         /* broadcast NodeInfo + telemetry now */
void wifi_svc_init(settings_t *reg);     /* WiFi + web UI from settings */
void vibe_svc_init(settings_t *reg, eventbus_t *bus);   /* motor + buzz on message */
void vibe_svc_test(void);                /* one buzz, for checking the wiring */
void led_svc_init(settings_t *reg, eventbus_t *bus);    /* D1 notification patterns */
void led_svc_test(void);                 /* one solid second, for checking the wiring */
void status_svc_init(settings_t *reg);   /* creates the sidebar lv_timer */

#endif /* SERVICES_SERVICES_H */
