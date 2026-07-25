/* Tilt-compensated compass checks (util/compass). Samples are synthesised from a
 * known yaw/roll/pitch with the forward model the heading maths inverts, so the
 * expected answer is the yaw that went in. Field: 20 uT north, 44 uT down --
 * roughly central Europe, where the vertical component dominates and a wrong
 * de-rotation shows up as tens of degrees. */
#include "test_util.h"
#include "util/compass.h"

#include <math.h>

#define D2R (3.14159265358979323846 / 180.0)
#define BH  20.0    /* horizontal field, uT */
#define BV  44.0    /* downward field, uT */

/* Rotate a vector from the level, yaw-aligned frame into the badge frame:
 * Rx(-roll) Ry(-pitch), the inverse of the Z-Y-X attitude. */
static void to_body(double roll, double pitch, const double v[3], double out[3])
{
    double sr = sin(roll * D2R),  cr = cos(roll * D2R);
    double sp = sin(pitch * D2R), cp = cos(pitch * D2R);
    double w[3] = {cp * v[0] - sp * v[2], v[1], sp * v[0] + cp * v[2]};

    out[0] = w[0];
    out[1] =  cr * w[1] + sr * w[2];
    out[2] = -sr * w[1] + cr * w[2];
}

/* Accelerometer (g) and magnetometer (uT) a badge at this attitude would report:
 * gravity reads +1 g straight up, the field points north and down.
 *
 * roll and pitch here are the parameters of the forward rotation, so pitch runs
 * the other way from the nose-up pitch compass_attitude_deg reports: pitch = -40
 * is the badge with its nose 40 deg up. */
static void synth(double yaw, double roll, double pitch, double a[3], double m[3])
{
    const double up[3] = {0.0, 0.0, 1.0};
    const double field[3] = {BH * cos(yaw * D2R), BH * sin(yaw * D2R), -BV};

    to_body(roll, pitch, up, a);
    to_body(roll, pitch, field, m);
}

/* Heading for an attitude, or -1 when the maths refused the sample. */
static double head_at(double yaw, double roll, double pitch)
{
    double a[3], m[3], h = -1.0;
    synth(yaw, roll, pitch, a, m);
    if (!compass_heading_deg(a[0], a[1], a[2], m[0], m[1], m[2], &h)) return -1.0;
    return h;
}

/* Sample n of a sweep that visits every direction: the eight corners of a cube of
 * half-side r about the origin, taken in turn. Any run of sixteen covers all
 * eight octants, so a sweep built from this isolates the sample and span floors
 * instead of tripping the direction gate as well. */
static void sweep_add(compass_cal_t *c, unsigned n, double r)
{
    compass_cal_add(c, (n & 1) ? r : -r, (n & 2) ? r : -r, (n & 4) ? r : -r);
}

void run_compass(void)
{
    SUITE("compass/wrap");
    CHECK_NEAR(compass_wrap360(0.0), 0.0, 1e-12);
    CHECK_NEAR(compass_wrap360(359.9), 359.9, 1e-9);
    CHECK_NEAR(compass_wrap360(360.0), 0.0, 1e-9);       /* a full turn is north */
    CHECK_NEAR(compass_wrap360(-1.0), 359.0, 1e-9);
    CHECK_NEAR(compass_wrap360(-370.0), 350.0, 1e-9);    /* past a whole turn below */
    CHECK_NEAR(compass_wrap360(725.0), 5.0, 1e-9);       /* and above */

    SUITE("compass/ang-diff");
    CHECK_NEAR(compass_ang_diff(10.0, 350.0), 20.0, 1e-9);   /* across north the short way */
    CHECK_NEAR(compass_ang_diff(350.0, 10.0), 20.0, 1e-9);   /* symmetric in its arguments */
    CHECK_NEAR(compass_ang_diff(90.0, 90.0), 0.0, 1e-9);
    CHECK_NEAR(compass_ang_diff(0.0, 180.0), 180.0, 1e-9);   /* opposite: the 0..180 ceiling */

    SUITE("compass/level");
    /* Flat and level the tilt terms cancel, so the answer must equal both the
     * yaw that generated the sample and the plain atan2 of the field. */
    static const double yaws[] = {0.0, 12.5, 45.0, 90.0, 175.0, 180.0, 270.0, 359.5};
    for (unsigned i = 0; i < sizeof yaws / sizeof yaws[0]; i++) {
        double a[3], m[3], h = -1.0;
        synth(yaws[i], 0.0, 0.0, a, m);
        CHECK(compass_heading_deg(a[0], a[1], a[2], m[0], m[1], m[2], &h));
        double ref = atan2(m[1], m[0]) / D2R;
        if (ref < 0.0) ref += 360.0;
        CHECK_NEAR(h, ref, 1e-9);
        CHECK_NEAR(h, yaws[i], 1e-6);
        CHECK(h >= 0.0 && h < 360.0);   /* the contract is 0..360, never 360 itself */
    }

    SUITE("compass/tilt-invariance");
    /* The whole point of the feature: tilting the badge must not turn the
     * needle. A rotation applied in the wrong order or about the wrong axis
     * still passes the level test above but fails here by tens of degrees. */
    static const double tilted[] = {0.0, 37.0, 118.0, 205.0, 293.0, 359.5};
    for (unsigned i = 0; i < sizeof tilted / sizeof tilted[0]; i++) {
        double flat = head_at(tilted[i], 0.0, 0.0);
        CHECK_NEAR(compass_ang_diff(head_at(tilted[i],  25.0,   0.0), flat), 0.0, 1.0);
        CHECK_NEAR(compass_ang_diff(head_at(tilted[i], -25.0,   0.0), flat), 0.0, 1.0);
        CHECK_NEAR(compass_ang_diff(head_at(tilted[i],   0.0,  25.0), flat), 0.0, 1.0);
        CHECK_NEAR(compass_ang_diff(head_at(tilted[i],   0.0, -25.0), flat), 0.0, 1.0);
        CHECK_NEAR(compass_ang_diff(head_at(tilted[i],  20.0, -15.0), flat), 0.0, 1.0);
    }
    /* And the check bites: uncompensated, 25 deg of roll drags the needle far. */
    double ta[3], tm[3];
    synth(37.0, 25.0, 0.0, ta, tm);
    double naive = atan2(tm[1], tm[0]) / D2R;
    if (naive < 0.0) naive += 360.0;
    CHECK(compass_ang_diff(naive, 37.0) > 20.0);

    SUITE("compass/wrap-seam");
    /* Either side of north the heading is continuous, not a 360 deg jump. */
    CHECK_NEAR(head_at(359.9, 10.0, 10.0), 359.9, 0.5);
    CHECK_NEAR(head_at(0.1, 10.0, 10.0), 0.1, 0.5);
    CHECK_NEAR(compass_ang_diff(head_at(359.9, 0.0, 0.0), head_at(0.1, 0.0, 0.0)), 0.2, 1e-6);

    SUITE("compass/attitude");
    double a[3], m[3], roll = -1.0, pitch = -1.0;
    synth(0.0, 0.0, 0.0, a, m);
    compass_attitude_deg(a[0], a[1], a[2], &roll, &pitch);
    CHECK_NEAR(roll, 0.0, 1e-9);
    CHECK_NEAR(pitch, 0.0, 1e-9);           /* level: az carries the whole g */
    synth(70.0, 25.0, 0.0, a, m);
    compass_attitude_deg(a[0], a[1], a[2], &roll, &pitch);
    CHECK_NEAR(roll, 25.0, 1e-6);
    CHECK_NEAR(pitch, 0.0, 1e-6);           /* roll alone, whatever the yaw */
    /* Nose up 40 deg: gravity leans onto +X (forward), which must read as
     * positive pitch, the same sense as the left-side-up roll above. */
    synth(70.0, 0.0, -40.0, a, m);
    CHECK(a[0] > 0.0);                      /* forward axis tipped up into gravity */
    compass_attitude_deg(a[0], a[1], a[2], &roll, &pitch);
    CHECK_NEAR(roll, 0.0, 1e-6);
    CHECK_NEAR(pitch, 40.0, 1e-6);
    /* Nose down 30 deg with the right side lifted: both angles negative, and
     * neither leaks into the other. */
    synth(70.0, -15.0, 30.0, a, m);
    CHECK(a[0] < 0.0);
    compass_attitude_deg(a[0], a[1], a[2], &roll, &pitch);
    CHECK_NEAR(roll, -15.0, 1e-6);
    CHECK_NEAR(pitch, -30.0, 1e-6);

    SUITE("compass/declination");
    CHECK_NEAR(compass_true_deg(90.0, 0.0), 90.0, 1e-9);      /* no setting, no shift */
    CHECK_NEAR(compass_true_deg(90.0, 3.5), 93.5, 1e-9);      /* east declination is positive */
    CHECK_NEAR(compass_true_deg(10.0, -13.5), 356.5, 1e-9);   /* west is negative, wraps below north */
    CHECK_NEAR(compass_true_deg(350.0, 20.0), 10.0, 1e-9);    /* and wraps above it */

    SUITE("compass/cal-recovers-distortion");
    /* Sweep a 50 uT sphere through a known hard-iron offset and per-axis gain:
     * the six axis extremes, which set the box, plus the eight diagonals, which
     * are the directions a badge turned through every orientation also passes
     * through. The box centre must come back as the offset and the scales as the
     * gain ratios, so the corrected box is a cube on zero. */
    static const double dirs[14][3] = {
        { 1.0,  0.0,  0.0}, {-1.0,  0.0,  0.0},
        { 0.0,  1.0,  0.0}, { 0.0, -1.0,  0.0},
        { 0.0,  0.0,  1.0}, { 0.0,  0.0, -1.0},
        { 0.57735,  0.57735,  0.57735}, { 0.57735,  0.57735, -0.57735},
        { 0.57735, -0.57735,  0.57735}, { 0.57735, -0.57735, -0.57735},
        {-0.57735,  0.57735,  0.57735}, {-0.57735,  0.57735, -0.57735},
        {-0.57735, -0.57735,  0.57735}, {-0.57735, -0.57735, -0.57735},
    };
    const double off[3] = {12.0, -7.5, 3.0};
    const double gain[3] = {1.0, 1.25, 0.8};
    compass_cal_t cal;
    compass_cal_init(&cal);
    for (int k = 0; k < 240; k++) {
        const double *d = dirs[k % 14];
        compass_cal_add(&cal, 50.0 * d[0] * gain[0] + off[0],
                              50.0 * d[1] * gain[1] + off[1],
                              50.0 * d[2] * gain[2] + off[2]);
    }
    CHECK_EQI(cal.samples, 240);
    CHECK_EQI(cal.octants, 0xFF);             /* every direction seen */
    CHECK(compass_cal_done(&cal));
    /* Unequal spans are what real soft iron looks like (100/125/80 uT below), so
     * they are no reason to refuse the sweep. */
    CHECK(cal.max[2] - cal.min[2] < 0.7 * (cal.max[1] - cal.min[1]));

    double co[3] = {0.0, 0.0, 0.0}, cs[3] = {0.0, 0.0, 0.0};
    CHECK(compass_cal_result(&cal, co, cs));
    CHECK_NEAR(co[0], off[0], 1e-9);
    CHECK_NEAR(co[1], off[1], 1e-9);
    CHECK_NEAR(co[2], off[2], 1e-9);
    /* spans are 100/125/80 uT, so each scale is the mean span over its own */
    double mean_span = (100.0 + 125.0 + 80.0) / 3.0;
    CHECK_NEAR(cs[0], mean_span / 100.0, 1e-9);
    CHECK_NEAR(cs[1], mean_span / 125.0, 1e-9);
    CHECK_NEAR(cs[2], mean_span / 80.0, 1e-9);

    double lo[3] = {cal.min[0], cal.min[1], cal.min[2]};
    double hi[3] = {cal.max[0], cal.max[1], cal.max[2]};
    compass_cal_apply(co, cs, &lo[0], &lo[1], &lo[2]);
    compass_cal_apply(co, cs, &hi[0], &hi[1], &hi[2]);
    CHECK_NEAR(hi[0] - lo[0], hi[1] - lo[1], 1e-9);   /* box -> cube */
    CHECK_NEAR(hi[1] - lo[1], hi[2] - lo[2], 1e-9);
    CHECK_NEAR(hi[0] + lo[0], 0.0, 1e-9);             /* centred on zero */
    CHECK_NEAR(hi[1] + lo[1], 0.0, 1e-9);
    CHECK_NEAR(hi[2] + lo[2], 0.0, 1e-9);

    /* The correction turns a distorted sample back into a usable heading: the
     * leftover uniform scale is common to all three axes, so atan2 ignores it. */
    double da[3], dm[3], dh = -1.0;
    synth(70.0, 15.0, -20.0, da, dm);
    for (int i = 0; i < 3; i++) dm[i] = dm[i] * gain[i] + off[i];
    compass_cal_apply(co, cs, &dm[0], &dm[1], &dm[2]);
    CHECK(compass_heading_deg(da[0], da[1], da[2], dm[0], dm[1], dm[2], &dh));
    CHECK_NEAR(compass_ang_diff(dh, 70.0), 0.0, 1e-6);

    SUITE("compass/cal-thresholds");
    compass_cal_t few;
    compass_cal_init(&few);
    CHECK(!compass_cal_done(&few));           /* nothing swept yet */
    double so[3] = {9.0, 9.0, 9.0}, ss[3] = {9.0, 9.0, 9.0};
    CHECK(!compass_cal_result(&few, so, ss));
    CHECK_NEAR(so[0], 9.0, 0.0);              /* outputs untouched on refusal */
    CHECK_NEAR(ss[0], 9.0, 0.0);
    for (uint32_t i = 0; i < COMPASS_CAL_MIN_SAMPLES - 1; i++)
        sweep_add(&few, i, 30.0);
    CHECK_EQI(few.samples, COMPASS_CAL_MIN_SAMPLES - 1);
    CHECK_EQI(few.octants, 0xFF);             /* directions and span are in hand */
    CHECK(!compass_cal_done(&few));           /* wide enough, one sample short */
    sweep_add(&few, COMPASS_CAL_MIN_SAMPLES - 1, 30.0);
    CHECK(compass_cal_done(&few));            /* exactly at the sample floor */

    compass_cal_t box;
    compass_cal_init(&box);
    for (uint32_t i = 0; i < COMPASS_CAL_MIN_SAMPLES; i++)
        sweep_add(&box, i, COMPASS_CAL_MIN_SPAN_UT / 2.0);
    CHECK(compass_cal_done(&box));            /* span exactly at the floor counts */

    compass_cal_t narrow;
    compass_cal_init(&narrow);
    for (uint32_t i = 0; i < COMPASS_CAL_MIN_SAMPLES; i++)
        sweep_add(&narrow, i, (COMPASS_CAL_MIN_SPAN_UT - 0.1) / 2.0);
    CHECK_EQI(narrow.octants, 0xFF);          /* so only the span can be at fault */
    CHECK(!compass_cal_done(&narrow));        /* a hair under is not evidence */

    compass_cal_t flatz;
    compass_cal_init(&flatz);
    for (uint32_t i = 0; i < COMPASS_CAL_MIN_SAMPLES; i++) {
        /* All four quadrants of x and y, z pinned: the badge only spun flat. */
        compass_cal_add(&flatz, (i & 1) ? 30.0 : -30.0, (i & 2) ? 30.0 : -30.0, 5.0);
    }
    CHECK(!compass_cal_done(&flatz));         /* z never swept: no span, and no z+ */
    CHECK_NEAR(flatz.max[2] - flatz.min[2], 0.0, 0.0);
    CHECK_EQI(flatz.octants, 0x0F);           /* only the four z- octants reachable */

    SUITE("compass/cal-needs-directions");
    /* Rocking the badge through 70 deg of roll and pitch at a fixed yaw pushes
     * every per-axis span well past the floor, so the sample and span floors alone
     * would call this sweep done. The field never points into whole octants of the
     * badge frame though, and the centre of the swept patch is nowhere near the
     * hard-iron offset it is taken for. */
    compass_cal_t rock;
    compass_cal_init(&rock);
    for (int r = -35; r <= 35; r += 5) {
        for (int p = -35; p <= 35; p += 5) {
            double ra[3], rm[3];
            synth(0.0, r, p, ra, rm);
            compass_cal_add(&rock, rm[0], rm[1], rm[2]);
        }
    }
    CHECK(rock.samples >= COMPASS_CAL_MIN_SAMPLES);
    for (int i = 0; i < 3; i++)
        CHECK(rock.max[i] - rock.min[i] > COMPASS_CAL_MIN_SPAN_UT);
    CHECK(rock.octants != 0xFF);              /* directions overrule the spans */
    CHECK(!compass_cal_done(&rock));
    double ro[3] = {7.0, 7.0, 7.0}, rs[3] = {7.0, 7.0, 7.0};
    CHECK(!compass_cal_result(&rock, ro, rs));
    CHECK_NEAR(ro[0], 7.0, 0.0);
    /* And the refusal is worth having: these samples carry no distortion at all,
     * yet the box centre the gate rejected would shift a level heading by tens of
     * degrees. That error is the whole cost of accepting a partial sweep. */
    double bo[3], bs[3] = {1.0, 1.0, 1.0}, ba[3], bm[3], bh = -1.0;
    for (int i = 0; i < 3; i++) bo[i] = (rock.max[i] + rock.min[i]) / 2.0;
    synth(70.0, 0.0, 0.0, ba, bm);
    compass_cal_apply(bo, bs, &bm[0], &bm[1], &bm[2]);
    CHECK(compass_heading_deg(ba[0], ba[1], ba[2], bm[0], bm[1], bm[2], &bh));
    CHECK(compass_ang_diff(bh, 70.0) > 20.0);

    SUITE("compass/cal-ignores-non-measurements");
    /* A failed magnetometer read reaches this layer as zeroes. Absorbing one would
     * drag the box centre off by half the field for the rest of the sweep, so it
     * is neither counted nor folded in. */
    compass_cal_t zc;
    compass_cal_init(&zc);
    compass_cal_add(&zc, 0.0, 0.0, 0.0);
    CHECK_EQI(zc.samples, 0);
    CHECK(!zc.seeded);                        /* not even used as the seed */
    CHECK_EQI(zc.octants, 0);
    compass_cal_add(&zc, 30.0, -20.0, 10.0);
    CHECK_EQI(zc.samples, 1);
    CHECK(zc.seeded);
    compass_cal_add(&zc, 0.0, 0.0, 0.0);
    CHECK_EQI(zc.samples, 1);                 /* still one real sample */
    CHECK_NEAR(zc.min[0], 30.0, 0.0);         /* and the box has not moved */
    CHECK_NEAR(zc.max[0], 30.0, 0.0);
    CHECK_NEAR(zc.min[1], -20.0, 0.0);
    CHECK_NEAR(zc.min[2], 10.0, 0.0);
    compass_cal_add(&zc, 0.05, -0.05, 0.05);  /* under the floor: sensor noise */
    CHECK_EQI(zc.samples, 1);
    compass_cal_add(&zc, 0.0, COMPASS_CAL_MIN_UT * 2.0, 0.0);   /* over it: a reading */
    CHECK_EQI(zc.samples, 2);
    CHECK_NEAR(zc.min[1], -20.0, 0.0);
    CHECK_NEAR(zc.max[1], 0.2, 1e-12);

    /* A single absorbed zero used to be enough on its own: it spans half the
     * field on every axis at once. The floors must not be reachable that way. */
    compass_cal_t poison;
    compass_cal_init(&poison);
    for (uint32_t i = 0; i < COMPASS_CAL_MIN_SAMPLES; i++) {
        compass_cal_add(&poison, 40.0, 40.0, 40.0);   /* the badge never moved */
        compass_cal_add(&poison, 0.0, 0.0, 0.0);      /* a dropped read */
    }
    CHECK_EQI(poison.samples, COMPASS_CAL_MIN_SAMPLES);
    CHECK_NEAR(poison.max[0] - poison.min[0], 0.0, 0.0);
    CHECK(!compass_cal_done(&poison));

    SUITE("compass/lpf");
    compass_lpf_t f;
    compass_lpf_init(&f);
    CHECK_NEAR(compass_lpf_update(&f, 137.0, 0.2), 137.0, 1e-9);  /* seeded, not eased from 0 */
    CHECK_NEAR(compass_lpf_update(&f, 137.0, 0.2), 137.0, 1e-9);  /* a constant input stays put */
    double prev = 137.0;
    for (int i = 0; i < 60; i++) {
        double v = compass_lpf_update(&f, 177.0, 0.2);
        CHECK(v > prev - 1e-9 && v < 177.0 + 1e-9);   /* monotone, no overshoot */
        prev = v;
    }
    CHECK_NEAR(prev, 177.0, 0.01);            /* and it does arrive */

    SUITE("compass/lpf-seam");
    compass_lpf_t g;
    compass_lpf_init(&g);
    CHECK_NEAR(compass_lpf_update(&g, 350.0, 0.5), 350.0, 1e-9);
    /* Halfway from 350 to 10 is due north. Averaging the degrees instead of the
     * unit vector would put it at 180, pointing the map the wrong way. */
    double seam = compass_lpf_update(&g, 10.0, 0.5);
    CHECK(compass_ang_diff(seam, 0.0) < 1e-9);
    CHECK(seam >= 0.0 && seam < 360.0);
    for (int i = 0; i < 40; i++) {
        seam = compass_lpf_update(&g, 10.0, 0.5);
        CHECK(compass_ang_diff(seam, 5.0) < 15.0);    /* never leaves the short arc */
        CHECK(seam >= 0.0 && seam < 360.0);
    }
    CHECK_NEAR(compass_ang_diff(seam, 10.0), 0.0, 0.01);

    SUITE("compass/state");
    /* Not running -> OFF regardless of the rest. */
    CHECK_EQI(compass_state_from(false, 0, true), COMPASS_STATE_OFF);
    CHECK_EQI(compass_state_from(false, UINT32_MAX, false), COMPASS_STATE_OFF);
    /* Running but nothing arriving, or samples gone stale -> NO_DATA. */
    CHECK_EQI(compass_state_from(true, UINT32_MAX, true), COMPASS_STATE_NO_DATA);
    CHECK_EQI(compass_state_from(true, COMPASS_NO_DATA_MS + 1, true), COMPASS_STATE_NO_DATA);
    /* Samples flowing but no calibration -> UNCAL, including at the boundary. */
    CHECK_EQI(compass_state_from(true, 0, false), COMPASS_STATE_UNCAL);
    CHECK_EQI(compass_state_from(true, COMPASS_NO_DATA_MS, false), COMPASS_STATE_UNCAL);
    /* Fresh and calibrated -> OK. */
    CHECK_EQI(compass_state_from(true, 0, true), COMPASS_STATE_OK);
    CHECK_EQI(compass_state_from(true, COMPASS_NO_DATA_MS, true), COMPASS_STATE_OK);

    SUITE("compass/pick-up");
    double up = -1.0;
    /* Compass beats a perfectly good GPS course: it works standing still. */
    CHECK_EQI(compass_pick_up(true, true, 123.0, true, 10.0, 45.0, &up), COMPASS_SRC_MAG);
    CHECK_NEAR(up, 123.0, 1e-9);
    /* No compass, moving -> course over ground. */
    CHECK_EQI(compass_pick_up(true, false, 0.0, true, 5.0, 45.0, &up), COMPASS_SRC_GPS);
    CHECK_NEAR(up, 45.0, 1e-9);
    /* At the threshold, not above it: too slow for the course to mean anything. */
    CHECK_EQI(compass_pick_up(true, false, 0.0, true, COMPASS_MOVING_KN, 45.0, &up),
              COMPASS_SRC_NONE);
    CHECK_NEAR(up, 0.0, 1e-9);
    CHECK_EQI(compass_pick_up(true, false, 0.0, true, COMPASS_MOVING_KN + 0.01, 45.0, &up),
              COMPASS_SRC_GPS);
    CHECK_NEAR(up, 45.0, 1e-9);
    /* Moving fast but the fix reports no course -> north up. */
    CHECK_EQI(compass_pick_up(true, false, 0.0, false, 10.0, 45.0, &up), COMPASS_SRC_NONE);
    CHECK_NEAR(up, 0.0, 1e-9);
    /* Nothing at all -> north up. */
    CHECK_EQI(compass_pick_up(true, false, 0.0, false, 0.0, 0.0, &up), COMPASS_SRC_NONE);
    CHECK_NEAR(up, 0.0, 1e-9);
    /* The north-up lock wins over both sources. */
    CHECK_EQI(compass_pick_up(false, true, 123.0, true, 10.0, 45.0, &up), COMPASS_SRC_NONE);
    CHECK_NEAR(up, 0.0, 1e-9);
    CHECK_EQI(compass_pick_up(false, false, 0.0, true, 10.0, 45.0, &up), COMPASS_SRC_NONE);
    CHECK_NEAR(up, 0.0, 1e-9);
    /* Out-of-range angles come back wrapped, so the caller can rotate with them. */
    CHECK_EQI(compass_pick_up(true, true, 370.0, false, 0.0, 0.0, &up), COMPASS_SRC_MAG);
    CHECK_NEAR(up, 10.0, 1e-9);
    CHECK_EQI(compass_pick_up(true, false, 0.0, true, 3.0, -30.0, &up), COMPASS_SRC_GPS);
    CHECK_NEAR(up, 330.0, 1e-9);

    SUITE("compass/degenerate");
    double h = -12345.0;
    CHECK(!compass_heading_deg(0.0, 0.0, 0.0, BH, 0.0, -BV, &h));   /* free fall: no up */
    CHECK_NEAR(h, -12345.0, 0.0);                                   /* *out untouched */
    CHECK(!compass_heading_deg(0.0, 0.0, 0.01, BH, 0.0, -BV, &h));  /* under the noise floor */
    CHECK_NEAR(h, -12345.0, 0.0);
    CHECK(!compass_heading_deg(0.0, 0.0, 1.0, 0.0, 0.0, 0.0, &h));  /* no field at all */
    CHECK_NEAR(h, -12345.0, 0.0);
    CHECK(!compass_heading_deg(0.0, 0.0, 1.0, 0.05, 0.0, 0.05, &h));/* shielded magnetometer */
    CHECK_NEAR(h, -12345.0, 0.0);
    CHECK(compass_heading_deg(0.0, 0.0, 1.0, BH, 0.0, -BV, &h));    /* a real sample still works */
    CHECK_NEAR(h, 0.0, 1e-9);

    SUITE("compass/cardinal");
    /* Sector centres. */
    CHECK_STR(compass_cardinal(0.0), "N");
    CHECK_STR(compass_cardinal(45.0), "NE");
    CHECK_STR(compass_cardinal(90.0), "E");
    CHECK_STR(compass_cardinal(135.0), "SE");
    CHECK_STR(compass_cardinal(180.0), "S");
    CHECK_STR(compass_cardinal(225.0), "SW");
    CHECK_STR(compass_cardinal(270.0), "W");
    CHECK_STR(compass_cardinal(315.0), "NW");
    /* Every boundary belongs to the sector above it, all the way round. */
    CHECK_STR(compass_cardinal(22.5), "NE");
    CHECK_STR(compass_cardinal(22.4), "N");
    CHECK_STR(compass_cardinal(67.5), "E");
    CHECK_STR(compass_cardinal(112.5), "SE");
    CHECK_STR(compass_cardinal(157.5), "S");
    CHECK_STR(compass_cardinal(202.5), "SW");
    CHECK_STR(compass_cardinal(247.5), "W");
    CHECK_STR(compass_cardinal(292.5), "NW");
    CHECK_STR(compass_cardinal(337.5), "N");     /* the seam, from below */
    CHECK_STR(compass_cardinal(337.4), "NW");
    CHECK_STR(compass_cardinal(359.9), "N");
    /* Unnormalised input. -30 deg is 330 deg, which is NW; a cast that truncates
     * toward zero would call it sector 0 and print "N", and -90 would index the
     * table from the wrong end. */
    CHECK_STR(compass_cardinal(-30.0), "NW");
    CHECK_STR(compass_cardinal(-90.0), "W");
    CHECK_STR(compass_cardinal(-0.1), "N");
    CHECK_STR(compass_cardinal(370.0), "N");
    CHECK_STR(compass_cardinal(405.0), "NE");    /* past a whole turn above */
    CHECK_STR(compass_cardinal(-390.0), "NW");   /* and below */

    SUITE("compass/hint");
    /* A heading in use needs no advice, whichever source won. */
    CHECK_EQI(compass_hint(COMPASS_SRC_MAG, COMPASS_STATE_OK, true, true), COMPASS_HINT_NONE);
    CHECK_EQI(compass_hint(COMPASS_SRC_GPS, COMPASS_STATE_OFF, false, false), COMPASS_HINT_NONE);
    /* Nothing fitted, or switched off: only walking is left. */
    CHECK_EQI(compass_hint(COMPASS_SRC_NONE, COMPASS_STATE_OFF, false, false), COMPASS_HINT_MOVE);
    CHECK_EQI(compass_hint(COMPASS_SRC_NONE, COMPASS_STATE_OFF, true, true), COMPASS_HINT_MOVE);
    /* Fitted, never calibrated: a sweep in the Compass app. */
    CHECK_EQI(compass_hint(COMPASS_SRC_NONE, COMPASS_STATE_UNCAL, false, false), COMPASS_HINT_CAL);
    /* Stored but held back by mag_cal_use: another sweep would not help. */
    CHECK_EQI(compass_hint(COMPASS_SRC_NONE, COMPASS_STATE_UNCAL, true, false),
              COMPASS_HINT_CAL_OFF);
    /* Stored and in use, so an UNCAL reading is about the current sweep, not the
     * setting: that wants a sweep again. */
    CHECK_EQI(compass_hint(COMPASS_SRC_NONE, COMPASS_STATE_UNCAL, true, true), COMPASS_HINT_CAL);
    /* Fitted and calibrated, samples merely late. */
    CHECK_EQI(compass_hint(COMPASS_SRC_NONE, COMPASS_STATE_NO_DATA, true, true), COMPASS_HINT_WAIT);
    CHECK_EQI(compass_hint(COMPASS_SRC_NONE, COMPASS_STATE_NO_DATA, false, false),
              COMPASS_HINT_WAIT);
    CHECK_EQI(compass_hint(COMPASS_SRC_NONE, COMPASS_STATE_OK, true, true), COMPASS_HINT_WAIT);

    SUITE("compass/hint-text");
    CHECK_STR(compass_hint_text(COMPASS_HINT_NONE), "");
    CHECK_STR(compass_hint_text(COMPASS_HINT_MOVE), "move");
    CHECK_STR(compass_hint_text(COMPASS_HINT_CAL), "cal");
    CHECK_STR(compass_hint_text(COMPASS_HINT_CAL_OFF), "cal off");
    CHECK_STR(compass_hint_text(COMPASS_HINT_WAIT), "wait");

    SUITE("compass/up-label");
    /* North-up by the app's own choice carries no complaint, whatever is fitted. */
    CHECK_STR(compass_up_label(false, COMPASS_SRC_MAG, COMPASS_HINT_NONE), "North up");
    CHECK_STR(compass_up_label(false, COMPASS_SRC_NONE, COMPASS_HINT_CAL), "North up");
    /* Heading-up with a live source names the source. */
    CHECK_STR(compass_up_label(true, COMPASS_SRC_MAG, COMPASS_HINT_NONE), "Heading up");
    CHECK_STR(compass_up_label(true, COMPASS_SRC_GPS, COMPASS_HINT_NONE), "Course up");
    /* Heading-up with nothing to turn to: say why, so the advice is actionable. */
    CHECK_STR(compass_up_label(true, COMPASS_SRC_NONE, COMPASS_HINT_MOVE), "North up (move)");
    CHECK_STR(compass_up_label(true, COMPASS_SRC_NONE, COMPASS_HINT_CAL), "North up (cal)");
    CHECK_STR(compass_up_label(true, COMPASS_SRC_NONE, COMPASS_HINT_CAL_OFF),
              "North up (cal off)");
    CHECK_STR(compass_up_label(true, COMPASS_SRC_NONE, COMPASS_HINT_WAIT), "North up (wait)");
    CHECK_STR(compass_up_label(true, COMPASS_SRC_NONE, COMPASS_HINT_NONE), "North up");

    SUITE("compass/hint-wording");
    /* The two views of the same state must agree: pick_up chooses the source,
     * hint explains the gap, and both labels are built from that one answer. */
    double hu = -1.0;
    compass_src_t hs = compass_pick_up(true, false, 0.0, false, 0.0, 0.0, &hu);
    compass_hint_t hh = compass_hint(hs, COMPASS_STATE_OFF, false, false);
    CHECK_EQI(hs, COMPASS_SRC_NONE);
    CHECK_STR(compass_hint_text(hh), "move");
    CHECK_STR(compass_up_label(true, hs, hh), "North up (move)");
    hs = compass_pick_up(true, true, 12.0, false, 0.0, 0.0, &hu);
    hh = compass_hint(hs, COMPASS_STATE_OK, true, true);
    CHECK_STR(compass_hint_text(hh), "");
    CHECK_STR(compass_up_label(true, hs, hh), "Heading up");
}
