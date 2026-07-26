/* See util/compass.h. Heading is the classic two-step: roll and pitch come from
 * gravity, the field vector is de-rotated by them back into the horizontal
 * plane, and atan2 of what is left is the angle from magnetic north. The sensor
 * frame is the one imu_read() reports -- +X forward, +Y left, +Z out of the
 * badge face -- so lying flat and level reads az = +1 g and the heading
 * collapses to atan2(my, mx). */
#include "util/compass.h"

#include <math.h>

#define CP_PI  3.14159265358979323846
#define CP_D2R (CP_PI / 180.0)
#define CP_R2D (180.0 / CP_PI)

/* Noise floors, not zero tests: a tenth of a g or of a uT carries no direction,
 * only sensor noise, and atan2 of two of those is a random heading. */
#define CP_MIN_G  0.1
#define CP_MIN_UT 0.1

double compass_wrap360(double deg)
{
    /* fmod first: a heading that came out of a chain of additions (declination,
     * mounting offset) can be more than a full turn from the range, which the
     * bare +360 idiom would leave negative. */
    double d = fmod(deg, 360.0);
    return fmod(d + 360.0, 360.0);
}

double compass_ang_diff(double a, double b)
{
    double d = fmod(a - b + 540.0, 360.0) - 180.0;
    return d < 0 ? -d : d;
}

bool compass_heading_deg(double ax, double ay, double az,
                         double mx, double my, double mz, double *out)
{
    double amag = sqrt(ax * ax + ay * ay + az * az);
    double mmag = sqrt(mx * mx + my * my + mz * mz);
    if (amag < CP_MIN_G || mmag < CP_MIN_UT)
        return false;                   /* free fall, or no field: no heading */

    /* Tilt from gravity alone. Both angles are ratios of accelerometer axes, so
     * they do not care about the magnitude checked above. */
    double roll  = atan2(ay, az);
    double pitch = atan2(-ax, sqrt(ay * ay + az * az));

    double sr = sin(roll),  cr = cos(roll);
    double sp = sin(pitch), cp = cos(pitch);

    /* Undo pitch then roll (the Z-Y-X order the attitude was measured in): what
     * is left of x and y is the horizontal field, pointing at magnetic north.
     * Near +-90 deg of pitch the forward axis is vertical and its heading stops
     * meaning anything -- the caller sees that in the reported pitch. */
    double xh = mx * cp + sp * (my * sr + mz * cr);
    double yh = my * cr - mz * sr;

    *out = compass_wrap360(atan2(yh, xh) * CP_R2D);
    return true;
}

void compass_attitude_deg(double ax, double ay, double az,
                          double *roll_deg, double *pitch_deg)
{
    *roll_deg  = atan2(ay, az) * CP_R2D;
    /* Negated on the way out so the two reported angles share a sense: gravity
     * gives +ax when the nose rises, and atan2(-ax, ..) turns that into a falling
     * number, which would read nose up as negative next to a left-side-up roll
     * that reads positive. The identically shaped pitch inside
     * compass_heading_deg is a different quantity -- the de-rotation angle paired
     * with xh/yh -- and must keep its own sign. */
    *pitch_deg = -atan2(-ax, sqrt(ay * ay + az * az)) * CP_R2D;
}

double compass_true_deg(double magnetic_deg, double declination_deg)
{
    return compass_wrap360(magnetic_deg + declination_deg);
}

bool compass_field_plausible(double mx, double my, double mz)
{
    double m = sqrt(mx * mx + my * my + mz * mz);
    return m >= COMPASS_FIELD_MIN_UT && m <= COMPASS_FIELD_MAX_UT;
}

void compass_cal_init(compass_cal_t *c)
{
    for (int i = 0; i < 3; i++) {
        c->min[i] = 0.0;
        c->max[i] = 0.0;
    }
    c->samples = 0;
    c->seeded = false;
    c->octants = 0;
}

void compass_cal_add(compass_cal_t *c, double mx, double my, double mz)
{
    const double m[3] = {mx, my, mz};

    /* Not a measurement: a dropped magnetometer read reaches callers as zeroes,
     * and absorbing one would drag the box centre off by half the field for the
     * rest of the sweep and pass the span floor on its own. Refused before the
     * seed, so it neither moves the box nor counts as a sample -- the service
     * filters on its own read flag too, but this layer must not be poisonable by
     * any caller. Same floor the heading maths uses.
     *
     * The upper bound matters just as much: a dead or counterfeit die answers on
     * the bus and returns noise across its whole range, and one such sample
     * widens the box by hundreds of uT for the rest of the sweep. */
    double mag = sqrt(mx * mx + my * my + mz * mz);
    if (mag < COMPASS_CAL_MIN_UT || mag > COMPASS_RAW_MAX_UT) return;

    if (!c->seeded) {
        /* Seed both extremes from the first sample. Starting from +-HUGE_VAL
         * would make a half-finished sweep look like an enormous box, and the
         * infinities would leak straight into the derived offset and scale. (Only
         * that derived record is persisted; compass_cal_t never reaches NVS.) */
        for (int i = 0; i < 3; i++) {
            c->min[i] = m[i];
            c->max[i] = m[i];
        }
        c->seeded = true;
    } else {
        for (int i = 0; i < 3; i++) {
            if (m[i] < c->min[i]) c->min[i] = m[i];
            if (m[i] > c->max[i]) c->max[i] = m[i];
        }
    }

    /* Which directions the field has been seen in: the sign of each axis about
     * the box centre so far is one bit of an octant index, and that index is one
     * bit of the coverage mask. The centre moves while the box grows, so coverage
     * is only ever added to. A mask short of all eight means whole directions were
     * never visited, which the per-axis spans cannot show: one axis rocked far
     * enough moves all three of them. */
    uint8_t oct = 0;
    for (int i = 0; i < 3; i++)
        if (m[i] > (c->min[i] + c->max[i]) / 2.0) oct |= (uint8_t)(1u << i);
    c->octants |= (uint8_t)(1u << oct);

    c->samples++;
}

bool compass_cal_done(const compass_cal_t *c)
{
    if (!c->seeded || c->samples < COMPASS_CAL_MIN_SAMPLES) return false;
    if (c->octants != 0xFFu) return false;   /* whole directions still unseen */
    for (int i = 0; i < 3; i++)
        if (c->max[i] - c->min[i] < COMPASS_CAL_MIN_SPAN_UT) return false;
    return true;
}

bool compass_cal_result(const compass_cal_t *c, double offset[3], double scale[3])
{
    if (!compass_cal_done(c)) return false;

    double span[3], mean = 0.0;
    for (int i = 0; i < 3; i++) {
        span[i] = c->max[i] - c->min[i];
        mean += span[i];
    }
    mean /= 3.0;

    for (int i = 0; i < 3; i++) {
        offset[i] = (c->max[i] + c->min[i]) / 2.0;   /* hard iron: the box centre */
        scale[i]  = mean / span[i];                  /* soft iron: box -> cube */
    }
    return true;
}

void compass_cal_apply(const double offset[3], const double scale[3],
                       double *mx, double *my, double *mz)
{
    *mx = (*mx - offset[0]) * scale[0];
    *my = (*my - offset[1]) * scale[1];
    *mz = (*mz - offset[2]) * scale[2];
}

void compass_lpf_init(compass_lpf_t *f)
{
    f->s = 0.0;
    f->c = 0.0;
    f->seeded = false;
}

double compass_lpf_update(compass_lpf_t *f, double deg, double alpha)
{
    double r = deg * CP_D2R;
    double s = sin(r), c = cos(r);

    if (!f->seeded) {
        /* Seed with the first heading: easing up from (0,0) would swing the view
         * in from north for the first second of every session. */
        f->s = s;
        f->c = c;
        f->seeded = true;
    } else {
        if (alpha < 0.0) alpha = 0.0;
        if (alpha > 1.0) alpha = 1.0;
        f->s += alpha * (s - f->s);
        f->c += alpha * (c - f->c);
    }
    return compass_wrap360(atan2(f->s, f->c) * CP_R2D);
}

compass_state_t compass_state_from(bool running, uint32_t ms_since_sample,
                                   bool calibrated, bool field_bad)
{
    if (!running) return COMPASS_STATE_OFF;
    if (ms_since_sample == UINT32_MAX || ms_since_sample > COMPASS_NO_DATA_MS)
        return COMPASS_STATE_NO_DATA;
    if (field_bad) return COMPASS_STATE_BAD_FIELD;
    if (!calibrated) return COMPASS_STATE_UNCAL;
    return COMPASS_STATE_OK;
}

compass_src_t compass_pick_up(bool allow_heading_up,
                              bool compass_ok, double compass_deg,
                              bool has_course, double speed_kn, double course_deg,
                              double *up_deg)
{
    /* The north-up lock wins: an app that is not in heading-up mode stays north
     * up even with a perfect heading on offer. */
    if (allow_heading_up) {
        if (compass_ok) {
            *up_deg = compass_wrap360(compass_deg);
            return COMPASS_SRC_MAG;
        }
        /* Course over ground is only a heading while actually moving -- standing
         * still it is whatever direction the fix last wandered in. */
        if (has_course && speed_kn > COMPASS_MOVING_KN) {
            *up_deg = compass_wrap360(course_deg);
            return COMPASS_SRC_GPS;
        }
    }
    *up_deg = 0.0;
    return COMPASS_SRC_NONE;
}

compass_hint_t compass_hint(compass_src_t src, compass_state_t state,
                            bool cal_stored, bool cal_in_use)
{
    /* A heading is a heading: GPS course counts, so there is nothing to advise. */
    if (src != COMPASS_SRC_NONE) return COMPASS_HINT_NONE;
    /* Nothing fitted (or switched off): walking is the only way to get a heading,
     * and telling the user to wait for a part that is not there is a dead end. */
    if (state == COMPASS_STATE_OFF) return COMPASS_HINT_MOVE;
    if (state == COMPASS_STATE_UNCAL) {
        /* A stored calibration that mag_cal_use is holding back looks exactly
         * like an uncalibrated magnetometer from the reading alone, but another
         * sweep would not help: the setting is the fix. */
        return (cal_stored && !cal_in_use) ? COMPASS_HINT_CAL_OFF : COMPASS_HINT_CAL;
    }
    /* A sensor returning something that is not a field cannot be swept into
     * working, so BAD_FIELD must not map to CAL. It maps to MOVE, because GPS
     * course really is the only heading left on that badge, the same as having no
     * magnetometer at all. The Compass and Diagnostics pages name the real fault;
     * a one-word hint on a map view cannot. */
    if (state == COMPASS_STATE_BAD_FIELD) return COMPASS_HINT_MOVE;
    /* Fitted and calibrated, so the samples are only late: NO_DATA, and the
     * heading-up view that has not seen its first fused sample yet. */
    return COMPASS_HINT_WAIT;
}

const char *compass_hint_text(compass_hint_t hint)
{
    switch (hint) {
    case COMPASS_HINT_MOVE:    return "move";
    case COMPASS_HINT_CAL:     return "cal";
    case COMPASS_HINT_CAL_OFF: return "cal off";
    case COMPASS_HINT_WAIT:    return "wait";
    case COMPASS_HINT_NONE:    break;
    }
    return "";
}

const char *compass_up_label(bool heading_up, compass_src_t src, compass_hint_t hint)
{
    /* North up by the app's own choice is not a fault, so it carries no hint. */
    if (!heading_up) return "North up";
    if (src == COMPASS_SRC_MAG) return "Heading up";
    if (src == COMPASS_SRC_GPS) return "Course up";
    switch (hint) {
    case COMPASS_HINT_MOVE:    return "North up (move)";
    case COMPASS_HINT_CAL:     return "North up (cal)";
    case COMPASS_HINT_CAL_OFF: return "North up (cal off)";
    case COMPASS_HINT_WAIT:    return "North up (wait)";
    case COMPASS_HINT_NONE:    break;
    }
    return "North up";
}

const char *compass_cardinal(double deg)
{
    static const char *const pts[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};

    /* Wrap after the half-sector shift, not before: a cast truncates toward zero,
     * so a bearing of -20 would index sector 0 and read "N" instead of "NW", and
     * 350 + 22.5 would run off the end of the table. */
    int i = (int)(compass_wrap360(deg + 22.5) / 45.0);
    return pts[i & 7];
}
