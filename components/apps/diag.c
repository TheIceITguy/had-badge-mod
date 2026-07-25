/* Diagnostics app: per-module status (LoRa mesh, GPS, compass, WiFi, Bluetooth,
 * system).
 *
 * The "Heard" row under LoRa counts every valid LoRa frame the radio demodulates
 * on any channel, before the channel/decrypt filter. It is the key field when
 * messages are not getting through: if it climbs, the radio hears traffic. */
#include "apps/app_iface.h"
#include "ui/frame.h"
#include "ui/theme.h"
#include "ui/colors.h"
#include "ui/menubar.h"
#include "net/backend.h"
#include "net/message.h"
#include "drivers/gps.h"
#include "drivers/wifi.h"
#include "drivers/battery.h"
#include "ble/ble.h"
#include "services/compass.h"
#include "services/services.h"
#include "util/gps_state.h"
#include "util/compass.h"

#include <stdint.h>
#include <stdio.h>
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_system.h"

enum {
    R_NODE, R_RADIO, R_CHAN, R_RXTX, R_HEARD, R_SIG, R_PEERS,
    R_GPS, R_GPS_POS, R_GPS_DATA,
    R_CMP, R_CMP_HDG, R_CMP_TILT, R_CMP_DATA,
    R_WIFI, R_WIFI_IP,
    R_BLE,
    R_BATT, R_SYS,
    R_COUNT
};
static lv_obj_t *s_val[R_COUNT];

static void make_header(lv_obj_t *parent, const char *caps)
{
    lv_obj_t *h = lv_label_create(parent);
    lv_label_set_text(h, caps);
    lv_obj_set_style_text_color(h, theme_hex(C_ACCENT), 0);
    lv_obj_set_style_text_letter_space(h, 1, 0);
    lv_obj_set_style_pad_top(h, 3, 0);
}

static lv_obj_t *make_row(lv_obj_t *parent, const char *caption)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_width(box, LV_PCT(100));
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 1, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *cap = lv_label_create(box);
    lv_label_set_text(cap, caption);
    lv_obj_set_width(cap, 64);
    lv_obj_set_style_text_color(cap, theme_hex(C_TEXT_DIM), 0);
    lv_obj_t *val = lv_label_create(box);
    lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(val, 1);
    lv_obj_set_style_text_color(val, theme_hex(C_TEXT), 0);
    lv_label_set_text(val, "--");
    return val;
}

static void build(lv_obj_t **screen, lv_group_t *group)
{
    static frame_t f;
    frame_create(&f, "Diagnostics");
    lv_obj_set_flex_flow(f.body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(f.body, 1, 0);
    ui_scroll_focusable(f.body, group);

    make_header(f.body, "LORA MESH");
    s_val[R_NODE]  = make_row(f.body, "Node");
    s_val[R_RADIO] = make_row(f.body, "Radio");
    s_val[R_CHAN]  = make_row(f.body, "Channel");
    s_val[R_RXTX]  = make_row(f.body, "RX/TX");
    s_val[R_HEARD] = make_row(f.body, "Heard");
    s_val[R_SIG]   = make_row(f.body, "Signal");
    s_val[R_PEERS] = make_row(f.body, "Peers");

    make_header(f.body, "GPS");
    s_val[R_GPS]      = make_row(f.body, "Fix");
    s_val[R_GPS_POS]  = make_row(f.body, "Pos");
    s_val[R_GPS_DATA] = make_row(f.body, "Data");

    make_header(f.body, "COMPASS");
    s_val[R_CMP]      = make_row(f.body, "State");
    s_val[R_CMP_HDG]  = make_row(f.body, "Heading");
    s_val[R_CMP_TILT] = make_row(f.body, "Tilt");
    s_val[R_CMP_DATA] = make_row(f.body, "Data");

    make_header(f.body, "WIFI");
    s_val[R_WIFI]    = make_row(f.body, "State");
    s_val[R_WIFI_IP] = make_row(f.body, "IP");

    make_header(f.body, "BLUETOOTH");
    s_val[R_BLE] = make_row(f.body, "State");

    make_header(f.body, "SYSTEM");
    s_val[R_BATT] = make_row(f.body, "Battery");
    s_val[R_SYS]  = make_row(f.body, "Up/Heap");

    *screen = f.screen;
    menubar_set_labels("Buzz", "LED", "", "", "");
}

static void tick(void)
{
    char b[96], id[12];

    /* --- LoRa mesh --- */
    net_diag_t d;
    net_diag(&d);
    net_node_id_str(d.node, id);
    lv_label_set_text(s_val[R_NODE], id);
    snprintf(b, sizeof b, "%s %.3fMHz SF%d", d.region, d.freq_mhz, d.sf);
    lv_label_set_text(s_val[R_RADIO], b);
    snprintf(b, sizeof b, "%s  sync 0x%02X", d.channel, d.sync_word);
    lv_label_set_text(s_val[R_CHAN], b);
    snprintf(b, sizeof b, "%lu / %lu", (unsigned long)d.rx_count, (unsigned long)d.tx_count);
    lv_label_set_text(s_val[R_RXTX], b);
    snprintf(b, sizeof b, "%lu frames (any ch)", (unsigned long)d.rx_raw);
    lv_label_set_text(s_val[R_HEARD], b);
    snprintf(b, sizeof b, "RSSI %.0f  SNR %.1f", (double)d.last_rssi, (double)d.last_snr);
    lv_label_set_text(s_val[R_SIG], b);
    snprintf(b, sizeof b, "%d", d.peers);
    lv_label_set_text(s_val[R_PEERS], b);

    /* --- GPS --- */
    gps_status_t gst;
    gps_get_status(&gst);
    gps_state_t gstate = gps_state_from(gst.running, gst.ms_since_data, gst.ms_since_fix, gst.fix.valid);
    switch (gstate) {
    case GPS_STATE_OFF:
        lv_label_set_text(s_val[R_GPS], "off");
        lv_label_set_text(s_val[R_GPS_POS], "--");
        break;
    case GPS_STATE_NO_DATA:
        lv_label_set_text(s_val[R_GPS], "no data");
        lv_label_set_text(s_val[R_GPS_POS], "--");
        break;
    case GPS_STATE_SEARCHING:
        lv_label_set_text(s_val[R_GPS], "searching");
        lv_label_set_text(s_val[R_GPS_POS], "--");
        break;
    case GPS_STATE_FIX:
        snprintf(b, sizeof b, "fix, %d/%d sats  HDOP %.1f",
                 gst.fix.sats, gst.fix.sats_in_view, gst.fix.hdop);
        lv_label_set_text(s_val[R_GPS], b);
        snprintf(b, sizeof b, "%.5f, %.5f", gst.fix.lat, gst.fix.lon);
        lv_label_set_text(s_val[R_GPS_POS], b);
        break;
    }
    if (gstate == GPS_STATE_OFF) {
        lv_label_set_text(s_val[R_GPS_DATA], "--");
    } else if (gst.ms_since_data == UINT32_MAX) {
        snprintf(b, sizeof b, "%lu sent, none yet", (unsigned long)gst.sentences);
        lv_label_set_text(s_val[R_GPS_DATA], b);
    } else {
        snprintf(b, sizeof b, "%lu sent, %lus", (unsigned long)gst.sentences,
                 (unsigned long)(gst.ms_since_data / 1000));
        lv_label_set_text(s_val[R_GPS_DATA], b);
    }

    /* --- Compass --- */
    compass_status_t cst;
    compass_get_status(&cst);
    /* One state, published by the service, so this page and the Compass app cannot
     * disagree about what is wrong. */
    compass_state_t cstate = cst.state;
    switch (cstate) {
    case COMPASS_STATE_OFF:
        /* Only two causes reach here: the setting is off, or it is on and nothing
         * answered at boot. A silent magnetometer die still leaves the sampling
         * task running, so it reports as "no magnetometer" below, not here. */
        lv_label_set_text(s_val[R_CMP], cst.enabled ? "no IMU on SAO" : "disabled");
        lv_label_set_text(s_val[R_CMP_HDG], "--");
        break;
    case COMPASS_STATE_NO_DATA:
        /* A missing AK09916 also lands here, because the accelerometer half
         * answers and only the fused sample never appears -- name the die
         * instead of leaving the user to suspect the whole part. */
        lv_label_set_text(s_val[R_CMP], cst.mag_present ? "no data" : "no magnetometer");
        lv_label_set_text(s_val[R_CMP_HDG], "--");
        break;
    case COMPASS_STATE_UNCAL:
        if (cst.cal_active) {
            snprintf(b, sizeof b, "calibrating, %lu%s", (unsigned long)cst.cal_samples,
                     cst.cal_ready ? " (ready)" : "");
            lv_label_set_text(s_val[R_CMP], b);
        } else {
            /* A stored calibration that mag_cal_use is ignoring reaches here too,
             * and "uncalibrated" would send that user off to sweep again. */
            lv_label_set_text(s_val[R_CMP], cst.cal_stored && !cst.cal_in_use
                                            ? "cal saved, unused" : "uncalibrated");
        }
        lv_label_set_text(s_val[R_CMP_HDG], "--");   /* nothing is published yet */
        break;
    case COMPASS_STATE_OK:
        lv_label_set_text(s_val[R_CMP], "ok");
        /* Magnetic and declination next to the true heading: a heading that is
         * off by a constant is a wrong declination, and only this row shows it. */
        snprintf(b, sizeof b, "%.0f true  mag %.0f  dec %+.1f",
                 cst.reading.heading_deg, cst.reading.magnetic_deg, cst.declination_deg);
        lv_label_set_text(s_val[R_CMP_HDG], b);
        break;
    }
    /* Roll/pitch need the accelerometer only and carry their own freshness, so
     * they must not hang off the fused state: with a dead magnetometer this row is
     * the one that proves the other die is alive. */
    if (cst.ms_since_tilt < COMPASS_NO_DATA_MS) {   /* UINT32_MAX = never, so it fails too */
        snprintf(b, sizeof b, "roll %.0f  pitch %.0f",
                 cst.reading.roll_deg, cst.reading.pitch_deg);
        lv_label_set_text(s_val[R_CMP_TILT], b);
    } else {
        lv_label_set_text(s_val[R_CMP_TILT], "--");
    }
    /* Transport counters beside the fused ones, GPS-row style. The error count is
     * the driver's, not the service's: only the driver sees a mag-only I2C failure
     * (imu_read still returns a good accelerometer sample), and a valid WHO_AM_I
     * with reads and errors both climbing while smp stays 0 is a magnetometer die
     * that is not soldered down. */
    if (cstate == COMPASS_STATE_OFF && !cst.enabled) {
        lv_label_set_text(s_val[R_CMP_DATA], "--");
    } else if (cstate == COMPASS_STATE_OFF) {
        /* Enabled but bring-up failed: the raw WHO_AM_I separates an empty bus
         * (00) from something answering that is not an ICM-20948. */
        snprintf(b, sizeof b, "id %02X, no reads", cst.imu_whoami);
        lv_label_set_text(s_val[R_CMP_DATA], b);
    } else if (cst.ms_since_sample == UINT32_MAX) {
        snprintf(b, sizeof b, "id %02X  %lu rd, %lu e  %lu smp, none yet",
                 cst.imu_whoami, (unsigned long)cst.imu_reads,
                 (unsigned long)cst.imu_errors, (unsigned long)cst.samples);
        lv_label_set_text(s_val[R_CMP_DATA], b);
    } else {
        snprintf(b, sizeof b, "id %02X  %lu rd, %lu e  %lu smp, %lus",
                 cst.imu_whoami, (unsigned long)cst.imu_reads,
                 (unsigned long)cst.imu_errors, (unsigned long)cst.samples,
                 (unsigned long)(cst.ms_since_sample / 1000));
        lv_label_set_text(s_val[R_CMP_DATA], b);
    }

    /* --- WiFi --- */
    char st[24]; int wr = 0; bool wrv = false;
    wifi_get_state(st, sizeof st, &wr, &wrv);
    if (wrv) snprintf(b, sizeof b, "%s  %d dBm", st, wr);
    else snprintf(b, sizeof b, "%s", st);
    lv_label_set_text(s_val[R_WIFI], b);
    if (wifi_link_up()) { char ip[20]; wifi_ip_str(ip, sizeof ip); lv_label_set_text(s_val[R_WIFI_IP], ip); }
    else lv_label_set_text(s_val[R_WIFI_IP], "--");

    /* --- Bluetooth --- */
    bool ben = false, bcon = false;
    ble_status(&ben, &bcon);
    lv_label_set_text(s_val[R_BLE], !ben ? "off" : (bcon ? "connected" : "advertising"));

    /* --- System --- */
    battery_state_t bat;
    if (battery_read(&bat) && bat.present)
        snprintf(b, sizeof b, "%d%%  %.2fV%s", bat.pct, (double)bat.volts, bat.usb ? " USB" : "");
    else
        snprintf(b, sizeof b, "disabled");
    lv_label_set_text(s_val[R_BATT], b);
    uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);
    snprintf(b, sizeof b, "%lum%02lus  %luK", (unsigned long)(up / 60),
             (unsigned long)(up % 60), (unsigned long)(esp_get_free_heap_size() / 1024));
    lv_label_set_text(s_val[R_SYS], b);
}

/* The motor and the LED are the two peripherals whose wiring cannot be confirmed
 * by reading a row: either they move and light up or the joint is bad. F1 and F2
 * fire them once so the answer takes a keypress instead of a message from
 * someone else. Both are no-ops when the feature is off in Settings. */
static void on_fkey(int n)
{
    if (n == 1) vibe_svc_test();
    else if (n == 2) led_svc_test();
}

const app_def_t *app_diag(void)
{
    static const app_def_t def = {
        .name = "Diag", .icon = LV_SYMBOL_EYE_OPEN,
        .build = build, .on_fkey = on_fkey, .tick = tick,
    };
    return &def;
}
