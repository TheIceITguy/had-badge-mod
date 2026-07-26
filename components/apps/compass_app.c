/* Compass app: the live tilt-compensated heading, plus the magnetometer
 * calibration sweep that makes it worth reading.
 *
 * The Source row is the point of the page: every heading-up view arbitrates
 * through compass_pick_up(), so this app names the winner (compass, GPS course,
 * or nothing) instead of implying the IMU is always the one being used. F1 runs
 * the sweep because an uncorrected magnetometer reads tens of degrees out and
 * only the user turning the badge can fix that -- the service refuses to save a
 * sweep that has not seen enough of every axis, and the hint label carries that
 * refusal back. */
#include "apps/app_iface.h"
#include "ui/frame.h"
#include "ui/theme.h"
#include "ui/colors.h"
#include "ui/menubar.h"
#include "services/compass.h"
#include "drivers/gps.h"
#include "util/compass.h"

#include <stdio.h>

/* The resting hints. The failure/success texts written over them by on_fkey must
 * survive until the next press, so tick() never touches this label. */
#define HINT_IDLE  "F1 calibrates: turn the badge slowly through every orientation, in a figure of eight, away from magnets, metal and speakers."
#define HINT_SWEEP "Keep turning in a figure of eight until every axis has been swept end to end. F1 saves the sweep, F2 abandons it."

static lv_obj_t *s_state, *s_hdg, *s_src, *s_mag, *s_att, *s_cal, *s_hint;

/* The bad-field hint carries a measured number, so unlike the other hints it
 * cannot be a literal. LVGL copies label text, but the buffer is static anyway
 * so nothing depends on that. */
static char s_hint_buf[256];

/* A sweep is only offered while a magnetometer the badge itself corrects is
 * answering. The F1 label and the F1 action both ask this, so they cannot
 * disagree. A BNO055 calibrates itself continuously and the badge never applies a
 * correction to it, so offering a sweep there would promise something the button
 * cannot do. */
static bool can_calibrate(const compass_status_t *st)
{
    return st->running && st->mag_present && st->mag_source != MAG_SOURCE_BNO055;
}

/* Every label is derived, never remembered: the sweep lives in the service, so
 * leaving the app and coming back mid-sweep must still offer Save. Kept to seven
 * caps characters or so -- a menubar cell is SCREEN_W/5 wide and clips to dots. */
static void refresh_menubar(void)
{
    compass_status_t st;
    compass_get_status(&st);
    if (st.cal_active) menubar_set_labels("Save", "Cancel", "", "", "Back");
    else menubar_set_labels(can_calibrate(&st) ? "Cal mag" : "", "", "", "", "Back");
}

static void reset_hint(void)
{
    compass_status_t st;
    compass_get_status(&st);
    if (st.cal_active) {
        lv_label_set_text(s_hint, HINT_SWEEP);
    } else if (st.state == COMPASS_STATE_BAD_FIELD) {
        /* Ahead of the calibration branches on purpose. Sweeping is exactly the
         * wrong instruction here, and it is the one the user will otherwise try
         * repeatedly, since a spinning heading looks like bad calibration. */
        snprintf(s_hint_buf, sizeof s_hint_buf,
                 "The magnetometer is answering but not measuring: %.0f uT, when the earth's field is "
                 "25 to 65 uT anywhere. Calibration cannot fix this. Either something magnetic is "
                 "against the sensor, or the die is dead (counterfeit ICM-20948 modules are common).",
                 st.field_ut);
        lv_label_set_text(s_hint, s_hint_buf);
    } else if (st.mag_source == MAG_SOURCE_BNO055) {
        /* Ahead of the can_calibrate branches, which would otherwise report this as
         * a missing magnetometer. Nothing is wrong here and there is nothing to
         * press: the part is calibrated by moving the badge, not by a sweep the
         * badge controls, and Diagnostics carries the four figures that say how far
         * along that is. */
        lv_label_set_text(s_hint, "A BNO055 fuses its own heading and calibrates itself as the badge "
                                  "moves, so there is no sweep to run. Turn the badge through a "
                                  "figure of eight to bring its magnetometer up; Diagnostics shows "
                                  "the calibration figures, and a heading appears once the "
                                  "magnetometer reaches 2 of 3.");
    } else if (!can_calibrate(&st)) {
        /* Three ways to have nothing to calibrate and three unrelated fixes, so the
         * cause has to be named: the magnetometer die, the setting, or the header.
         * "Turn on imu_enabled" is useless to a user whose setting is already on. */
        if (st.running)
            lv_label_set_text(s_hint, "The IMU answered but its magnetometer did not: no heading, and nothing to calibrate. Check the SAO wiring.");
        else if (!st.enabled)
            lv_label_set_text(s_hint, "Compass off. Turn on imu_enabled in Settings, then restart: the IMU is only brought up at boot.");
        else
            lv_label_set_text(s_hint, "imu_enabled is on, but nothing answered on the SAO header. Check SDA and SCL and the power at J8.");
    } else if (st.cal_stored && !st.cal_in_use) {
        /* Another sweep cannot fix a calibration the setting is ignoring, so point
         * at the setting instead of repeating the F1 invitation. */
        lv_label_set_text(s_hint, "A saved calibration is present but mag_cal_use is off in Settings, so the heading stays uncorrected and is never published. Turn that setting on, or press F1 to sweep again.");
    } else {
        lv_label_set_text(s_hint, HINT_IDLE);
    }
}

static lv_obj_t *row(lv_obj_t *parent, const char *caption)
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
    lv_obj_set_width(cap, 90);
    lv_obj_set_style_text_color(cap, theme_hex(C_TEXT_DIM), 0);
    lv_obj_t *val = lv_label_create(box);
    lv_label_set_text(val, "--");
    return val;
}

static void build(lv_obj_t **screen, lv_group_t *group)
{
    static frame_t f;
    frame_create(&f, "Compass");
    lv_obj_set_flex_flow(f.body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(f.body, 2, 0);
    ui_scroll_focusable(f.body, group);

    s_state = lv_label_create(f.body);
    lv_obj_set_style_text_font(s_state, theme_font_title(), 0);
    lv_label_set_text(s_state, "--");

    s_hdg = row(f.body, "Heading");
    s_src = row(f.body, "Source");
    s_mag = row(f.body, "Magnetic");
    s_att = row(f.body, "Roll/pitch");
    s_cal = row(f.body, "Calibration");

    s_hint = lv_label_create(f.body);
    lv_obj_set_style_text_color(s_hint, theme_hex(C_TEXT_DIM), 0);
    lv_obj_set_width(s_hint, LV_PCT(100));
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
    reset_hint();

    *screen = f.screen;
    refresh_menubar();
}

static void on_fkey(int n)
{
    compass_status_t st;
    compass_get_status(&st);
    if (n == 1) {
        if (st.cal_active) {
            if (compass_cal_save()) {
                /* The save publishes the correction at once, so cal_in_use now
                 * answers whether it will actually be applied. Promising a
                 * corrected heading while mag_cal_use is off promises a heading
                 * that is never published at all. */
                compass_get_status(&st);
                lv_label_set_text(s_hint, st.cal_in_use
                    ? "Calibration saved. The heading is corrected from the next sample."
                    : "Calibration saved, but mag_cal_use is off in Settings, so it stays unused and no heading is published. Turn that setting on to use it.");
            } else {
                lv_label_set_text(s_hint, "Not saved: the badge has not been turned through enough of every axis. Keep turning and press F1 again.");
            }
        } else if (can_calibrate(&st)) {
            compass_cal_begin();
            lv_label_set_text(s_hint, HINT_SWEEP);
        }
    } else if (n == 2) {
        if (st.cal_active) {
            compass_cal_cancel();
            reset_hint();
        }
    }
    refresh_menubar();
}

static void tick(void)
{
    compass_status_t st;
    compass_get_status(&st);
    /* The state is derived once, in the service, so this page and Diagnostics
     * cannot disagree about what is wrong. */
    compass_state_t state = st.state;
    char b[64];

    if (st.cal_active) {
        /* The sweep owns the status line while it runs: the sample count is the
         * only proof that turning the badge is doing anything. */
        snprintf(b, sizeof b, "Calibrating  %lu pts%s", (unsigned long)st.cal_samples,
                 st.cal_ready ? "  F1 saves" : "");
        lv_label_set_text(s_state, b);
        lv_obj_set_style_text_color(s_state, theme_hex(C_CRIT), 0);
    } else {
        switch (state) {
        /* Two ways to be off, and the fixes have nothing in common: the setting is
         * off, or it is on and nothing answered at boot. The hint below carries the
         * matching action. */
        case COMPASS_STATE_OFF:
            lv_label_set_text(s_state, st.enabled ? "No IMU on SAO" : "Off in Settings");
            break;
        /* A silent magnetometer die keeps the task sampling and only starves the
         * fused heading, so it lands here rather than in OFF. */
        case COMPASS_STATE_NO_DATA:
            lv_label_set_text(s_state, st.mag_present ? "No data" : "No magnetometer");
            break;
        case COMPASS_STATE_UNCAL:   lv_label_set_text(s_state, "Uncalibrated"); break;
        /* Samples are arriving and are not a magnetic field. Naming the fault
         * matters more here than anywhere: the symptom on the old firmware was a
         * heading that span, which reads as a software bug and sends people to
         * calibrate again instead of at the sensor. */
        case COMPASS_STATE_BAD_FIELD: lv_label_set_text(s_state, "Bad field"); break;
        case COMPASS_STATE_OK:      lv_label_set_text(s_state, "Ready"); break;
        }
        lv_obj_set_style_text_color(s_state, theme_hex(C_TEXT), 0);
    }

    /* One arbitration call answers both "which heading" and "whose", using the
     * reading the status snapshot already carries. */
    gps_fix_t fix;
    bool havefix = gps_get_fix(&fix) && fix.valid;
    double up = 0.0;
    compass_src_t src = compass_pick_up(true, st.reading.valid, st.reading.heading_deg,
                                        havefix && fix.has_course, fix.speed, fix.course, &up);
    if (src == COMPASS_SRC_NONE) {
        lv_label_set_text(s_hdg, "--");
    } else {
        snprintf(b, sizeof b, "%.0f deg  %s", up, compass_cardinal(up));
        lv_label_set_text(s_hdg, b);
    }
    lv_label_set_text(s_src, src == COMPASS_SRC_MAG ? "Compass"
                             : src == COMPASS_SRC_GPS ? "GPS course" : "None");

    /* The magnetic heading keeps updating through a sweep, when the published
     * heading is deliberately not valid yet. */
    if (state == COMPASS_STATE_OK || state == COMPASS_STATE_UNCAL) {
        snprintf(b, sizeof b, "%.0f deg, decl %+.1f", st.reading.magnetic_deg, st.declination_deg);
        lv_label_set_text(s_mag, b);
    } else {
        lv_label_set_text(s_mag, "--");
    }

    /* Roll and pitch come from the accelerometer alone and have their own
     * freshness, so they survive a dead magnetometer: this row is what tells the
     * user which of the two dies failed. */
    if (st.ms_since_tilt < COMPASS_NO_DATA_MS) {   /* UINT32_MAX = never, so it fails too */
        snprintf(b, sizeof b, "%.0f / %.0f deg", st.reading.roll_deg, st.reading.pitch_deg);
        lv_label_set_text(s_att, b);
    } else {
        lv_label_set_text(s_att, "--");
    }

    /* cal_ready is the only thing that tells the user a sweep can be saved, so it
     * gets its own row rather than living in the status line alone. A stored
     * calibration that mag_cal_use is keeping out of the loop has to say so: it is
     * neither absent nor working, and offering a sweep instead would not help. */
    if (state == COMPASS_STATE_OFF) lv_label_set_text(s_cal, "--");
    else if (st.mag_source == MAG_SOURCE_BNO055) {
        snprintf(b, sizeof b, "self, mag %u/3", st.bno_calib.mag);
        lv_label_set_text(s_cal, b);
    }
    else if (st.cal_active) lv_label_set_text(s_cal, st.cal_ready ? "Ready to save" : "Sweeping, keep turning");
    else if (!st.cal_stored) lv_label_set_text(s_cal, "None, press F1");
    else lv_label_set_text(s_cal, st.cal_in_use ? "Saved, in use" : "Saved, mag_cal_use off");
}

const app_def_t *app_compass(void)
{
    static const app_def_t def = {
        /* LVGL has no compass glyph; the rotation arrow is the closest read, and
         * it doubles as "turn the badge" for the calibration sweep. */
        .name = "Compass", .icon = LV_SYMBOL_REFRESH,
        .build = build, .on_fkey = on_fkey, .tick = tick,
    };
    return &def;
}
