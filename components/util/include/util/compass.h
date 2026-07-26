/* Tilt-compensated compass math: heading from an accelerometer plus a
 * magnetometer, hard/soft-iron calibration, circular smoothing, and the
 * heading-source choice the map-style apps share. Portable and host-tested
 * (no LVGL/IDF). */
#ifndef UTIL_COMPASS_H
#define UTIL_COMPASS_H

#include <stdbool.h>
#include <stdint.h>

/* Normalise an angle to 0..360. */
double compass_wrap360(double deg);

/* Smallest absolute difference between two headings, 0..180. */
double compass_ang_diff(double a, double b);

/* Tilt-compensated magnetic heading in degrees clockwise from magnetic north
 * (0..360). Accelerometer components in g, magnetometer in uT, both in the
 * sensor frame. Returns false for a degenerate input (free fall, or no field),
 * leaving *out untouched, because atan2 of two zeroes is not a heading. */
bool compass_heading_deg(double ax, double ay, double az,
                         double mx, double my, double mz, double *out);

/* Roll and pitch from gravity alone, in degrees: positive pitch is nose up (the
 * badge's top edge lifted), positive roll is left side up. Reported for
 * diagnostics; the heading above derives its own tilt terms. */
void compass_attitude_deg(double ax, double ay, double az,
                          double *roll_deg, double *pitch_deg);

/* True heading from a magnetic one. East declination is positive. The badge
 * carries no magnetic model, so the declination is a user setting. */
double compass_true_deg(double magnetic_deg, double declination_deg);

/* --- Hard/soft-iron calibration --------------------------------------------
 * The badge is turned through every orientation while samples accumulate; the
 * correction is the centre and the extent of the swept box. */

#define COMPASS_CAL_MIN_SAMPLES 200u    /* below this the sweep is not evidence */
#define COMPASS_CAL_MIN_SPAN_UT 20.0    /* per-axis span that proves rotation */
#define COMPASS_CAL_MIN_UT      0.1     /* a shorter vector is not a measurement */

/* --- Is this vector even a magnetic field? --------------------------------
 * The earth's total field is between about 25 and 65 uT everywhere on the
 * planet, so a reading far outside that is not a field at all. It is a dead or
 * counterfeit magnetometer die returning noise, saturation, or a magnet sitting
 * against the sensor. Checking this matters because atan2 turns any pair of
 * numbers into a confident-looking heading, so without it the badge renders
 * nonsense as a bearing and the user has no way to tell. */

/* Bounds for a CORRECTED sample, generous enough for an imperfect calibration. */
#define COMPASS_FIELD_MIN_UT  10.0
#define COMPASS_FIELD_MAX_UT  120.0

/* Bound for a RAW sample, which still carries hard iron. A badge next to a
 * speaker magnet might read a couple of hundred uT and calibrate out of it; the
 * AK09916 saturates at 4912 uT, and a broken die reads noise across that range. */
#define COMPASS_RAW_MAX_UT    400.0

/* True when a CORRECTED vector could plausibly be the earth's field. */
bool compass_field_plausible(double mx, double my, double mz);

/* True when a RAW vector could plausibly be the earth's field plus hard iron.
 * Judging the sensor and judging the calibration are different questions: a
 * stored correction that is wrong makes a healthy sensor look broken, and
 * blaming the part for that sends the user shopping instead of re-sweeping. */
bool compass_raw_plausible(double mx, double my, double mz);

typedef struct {
    double min[3], max[3];   /* per-axis extremes seen, uT */
    uint32_t samples;
    bool seeded;             /* the first sample initialises min and max */
    /* Octants of the field direction visited, one bit each. Span alone cannot
     * tell a full sweep from a tilt: rocking the badge through 60 degrees on one
     * axis moves every span past the floor while leaving whole directions
     * unseen, and the box centre of a half-sphere is not the hard-iron offset. */
    uint8_t octants;
} compass_cal_t;

void compass_cal_init(compass_cal_t *c);

/* Fold one raw sample into the sweep. A vector shorter than COMPASS_CAL_MIN_UT
 * is ignored rather than absorbed: a dropped magnetometer read reaches callers
 * as zeroes, and one of those would drag the box centre off by half the field. */
void compass_cal_add(compass_cal_t *c, double mx, double my, double mz);

/* True once the sweep covers enough samples, span and directions to trust the
 * extremes on every axis. */
bool compass_cal_done(const compass_cal_t *c);

/* Hard-iron offset (box centre) and soft-iron per-axis scale (normalised to the
 * mean span). Returns false while the sweep is not usable, leaving the outputs
 * untouched. */
bool compass_cal_result(const compass_cal_t *c, double offset[3], double scale[3]);

/* Apply a stored correction to one raw sample, in place. */
void compass_cal_apply(const double offset[3], const double scale[3],
                       double *mx, double *my, double *mz);

/* --- Smoothing ------------------------------------------------------------- */

/* Circular low-pass: a scalar average of degrees jumps at the 0/360 seam, so
 * the heading's unit vector is filtered instead and the angle recovered after.
 * alpha is the weight of the new sample, 0..1, derived by the caller from its
 * own sample rate. */
typedef struct { double s, c; bool seeded; } compass_lpf_t;

void compass_lpf_init(compass_lpf_t *f);
double compass_lpf_update(compass_lpf_t *f, double deg, double alpha);

/* --- Derived state -------------------------------------------------------- */

typedef enum {
    COMPASS_STATE_OFF,       /* disabled in Settings, or no sensor found */
    COMPASS_STATE_NO_DATA,   /* sampling, but nothing is arriving */
    COMPASS_STATE_UNCAL,     /* samples flowing, magnetometer not calibrated */
    COMPASS_STATE_BAD_FIELD, /* samples arriving, but they are not a magnetic field */
    COMPASS_STATE_OK,        /* fresh, calibrated heading */
} compass_state_t;

#define COMPASS_NO_DATA_MS 2000u   /* no sample for this long -> NO_DATA */

/* Derive the coarse state. ms_since_sample uses UINT32_MAX for "never".
 * field_bad reports that samples are arriving but fail compass_field_plausible,
 * which outranks the calibration question: no sweep can fix a sensor that is not
 * measuring, so telling the user to calibrate would send them the wrong way. */
compass_state_t compass_state_from(bool running, uint32_t ms_since_sample,
                                   bool calibrated, bool field_bad);

/* --- Heading source arbitration ------------------------------------------- */

typedef enum {
    COMPASS_SRC_NONE,   /* nothing usable: the view stays north-up */
    COMPASS_SRC_GPS,    /* GPS course over ground, only while moving */
    COMPASS_SRC_MAG,    /* tilt-compensated compass, valid standing still */
} compass_src_t;

#define COMPASS_MOVING_KN 1.0   /* GPS course counts as a heading above this */

/* Pick the up direction for a heading-up view: compass first, GPS course while
 * moving, otherwise north-up with *up_deg = 0. allow_heading_up is the app's
 * own north-up/heading-up toggle, so one call answers both questions. */
compass_src_t compass_pick_up(bool allow_heading_up,
                              bool compass_ok, double compass_deg,
                              bool has_course, double speed_kn, double course_deg,
                              double *up_deg);

/* --- Shared wording ------------------------------------------------------- */

/* Why a view is north-up, which decides what the user should do about it. Five
 * apps show this state, and telling someone to walk when the real answer is a
 * calibration sweep is the failure this enum exists to prevent. */
typedef enum {
    COMPASS_HINT_NONE,      /* a real heading is in use */
    COMPASS_HINT_MOVE,      /* no compass fitted: only GPS course is left */
    COMPASS_HINT_CAL,       /* fitted, needs a calibration sweep */
    COMPASS_HINT_CAL_OFF,   /* calibrated, but the saved calibration is disabled */
    COMPASS_HINT_WAIT,      /* fitted and calibrated, samples not arriving yet */
} compass_hint_t;

compass_hint_t compass_hint(compass_src_t src, compass_state_t state,
                            bool cal_stored, bool cal_in_use);

/* Parenthetical for the needle apps: "", "move", "cal", "cal off", "wait". */
const char *compass_hint_text(compass_hint_t hint);

/* Full up-direction label for the rotating views: "Heading up", "Course up",
 * "North up", or "North up (cal)" and friends. */
const char *compass_up_label(bool heading_up, compass_src_t src, compass_hint_t hint);

/* Nearest compass point ("N", "NE", ...) for a bearing in degrees. Wraps its
 * own input, so a caller may pass an unnormalised angle. */
const char *compass_cardinal(double deg);

#endif /* UTIL_COMPASS_H */
