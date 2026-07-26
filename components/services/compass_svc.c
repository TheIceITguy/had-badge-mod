/* See services/compass.h. IMU sample -> tilt-compensated heading -> circular
 * low-pass -> one published snapshot; the magnetometer correction lives in its
 * own NVS blob because the settings store only holds strings. */
#include "services/compass.h"
#include "services/services.h"
#include "drivers/imu.h"
#include "drivers/qmc5883l.h"
#include "util/compass.h"
#include "board_pins.h"

#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "compass";

#define SAMPLE_MS      50   /* 20 Hz: fast enough for a hand-held heading */
#define REFRESH_EVERY  20   /* samples between settings re-reads (~1 s) */

/* Weight of the new sample in the circular low-pass. At 20 Hz alpha = 0.25 puts
 * the time constant at SAMPLE_MS/alpha = 200 ms (90% of a step in ~0.45 s) and
 * scales per-sample noise by sqrt(a/(2-a)) ~ 0.38, so a badge lying still stays
 * inside the 3 deg map_canvas repaint threshold. The smoothing is load-bearing,
 * not cosmetic: ui/map_canvas.c re-streams the whole basemap from SPIFFS every
 * time the up direction moves more than MAP_CANVAS_UP_EPS (with only a 500 ms
 * floor), so an unsmoothed heading would repaint continuously. */
#define LPF_ALPHA      0.25

/* A sweep is bounded in time so an abandoned one cannot be saved much later.
 * The Compass app deliberately lets the user leave mid-sweep and come back, so
 * nothing in the UI ends it; past this budget the box is only widening with
 * unrelated field data, and saving that would write a wrong hard-iron offset to
 * NVS. A figure-of-eight takes seconds, so two minutes is already generous. */
#define CAL_MAX_MS     120000

/* Enum choices live above the schema because the schema points at them. Order is
 * load-bearing: the index into each array is the register field value. */
static const char *const MAG_SOURCE_CHOICES[] = { "imu", "qmc5883l" };
static const char *const QMC_RANGE_CHOICES[]  = { "2g", "8g" };
static const char *const QMC_OSR_CHOICES[]    = { "512", "256", "128", "64" };
static const char *const QMC_ODR_CHOICES[]    = { "10", "50", "100", "200" };

static const setting_t COMPASS_SCHEMA[] = {
    {.key = "imu_enabled", .type = SET_BOOL, .def = "true", .label = "IMU compass enabled",
     .group = "Compass"},
    {.key = "mag_decl_ddeg", .type = SET_INT, .def = "0", .label = "Declination (0.1 deg, E+)",
     .group = "Compass", .minv = -1800, .maxv = 1800, .has_min = true, .has_max = true},
    {.key = "mag_cal_use", .type = SET_BOOL, .def = "true", .label = "Use saved calibration",
     .group = "Compass"},
    /* The SAO header is the documented home for the sensor, but nothing stops it
     * being wired to any other free pair, so the bus is configurable the way the
     * GPS UART is. Defaults are the SAO pins from board_pins.h. */
    {.key = "imu_sda_pin", .type = SET_INT, .def = "4", .label = "Compass SDA pin",
     .group = "Compass", .minv = 0, .maxv = 48, .has_min = true, .has_max = true},
    {.key = "imu_scl_pin", .type = SET_INT, .def = "5", .label = "Compass SCL pin",
     .group = "Compass", .minv = 0, .maxv = 48, .has_min = true, .has_max = true},
    {.key = "imu_addr_hi", .type = SET_BOOL, .def = "false",
     .label = "I2C address 0x69 (AD0 high)", .group = "Compass"},
    /* Where the field comes from. The ICM's own AK09916 is the default because it
     * needs no extra hardware, but it has no oversampling control and on this badge
     * it reads with 200 to 400 uT of spread, so a separate part is selectable. The
     * accelerometer always comes from the ICM whatever this says: tilt compensation
     * needs it, and it has never been the problem. */
    {.key = "mag_source", .type = SET_ENUM, .def = "imu", .label = "Magnetometer",
     .group = "Compass", .choices = MAG_SOURCE_CHOICES, .nchoices = 2,
     .help = "imu = AK09916 inside the ICM-20948; qmc5883l = separate module at 0x0D"},
    /* 8 G rather than the usual 2 G: 2 G is 200 uT full scale and the interference
     * measured on this badge is 200 to 400 uT, so it would clip on the noise alone
     * and look like a broken sensor. */
    {.key = "qmc_range", .type = SET_ENUM, .def = "8g", .label = "QMC5883L range",
     .group = "Compass", .choices = QMC_RANGE_CHOICES, .nchoices = 2},
    /* Oversampling and output rate are exposed because the spread on this badge
     * varies with sample rate, so both are worth turning while diagnosing it. */
    {.key = "qmc_osr", .type = SET_ENUM, .def = "512", .label = "QMC5883L oversampling",
     .group = "Compass", .choices = QMC_OSR_CHOICES, .nchoices = 4},
    {.key = "qmc_odr", .type = SET_ENUM, .def = "100", .label = "QMC5883L rate (Hz)",
     .group = "Compass", .choices = QMC_ODR_CHOICES, .nchoices = 4},
};

static settings_t *s_reg;
static compass_reading_t s_reading;
static volatile bool s_running;
static compass_lpf_t s_lpf;

/* Activity counters for diagnostics; these count fused headings, not I2C reads
 * (imu_get_status has the transport view). Timestamps are esp_timer micros,
 * 0 = never. The attitude has its own: it comes from gravity alone and keeps
 * updating when only the magnetometer die is broken. */
static uint32_t s_samples, s_errors;

/* A magnetometer whose die is dead or counterfeit answers on the bus, reports
 * data-ready, and returns noise. Counting the physically impossible samples is
 * what turns that from "the compass is jittery" into a named fault. */
static uint32_t s_implausible;
static double s_field_ut, s_field_raw_ut;

/* Magnetometer self-test, run on request by the task that owns imu_read(). The
 * die measures a coil on its own substrate, so this is the only check that
 * separates a broken sensor from a magnetic desk, a bad SAO joint or a stale
 * calibration -- all four produce the same spinning heading. */
static volatile bool s_selftest_req, s_selftest_done;
static imu_mag_selftest_t s_selftest;
static volatile bool s_cal_bad;
static int64_t s_bad_log_us;
static volatile bool s_field_bad;
static int64_t s_last_sample_us, s_last_tilt_us;

/* Which part supplies the field. Fixed at start, because it decides what was
 * brought up at boot; changing the setting needs a restart the way the pins do. */
static mag_source_t s_mag_source;

/* Live settings, re-read periodically instead of per sample. */
static double s_decl_deg;
static bool s_use_cal;

/* imu_enabled as read at boot, captured before the early returns so the status
 * can separate "switched off in Settings" from "no IMU fitted". Kept in RAM
 * because compass_get_status is polled at 10 Hz and must not touch NVS. */
static bool s_enabled;

/* Stored correction, published by cal_use and read by the sampling task;
 * s_calibrated gates its use. What keeps the sampler from reading a half-written
 * pair is the scheduling, not the flag order: cal_use runs in the UI task
 * (priority 4, core 1) and the sampler is priority 3 pinned to the same core, so
 * the writer always runs to completion before the sampler can run again.
 * Clearing the flag first cannot stop an apply already in flight, because the
 * task latches the flag before it reads the arrays; all it does is keep a later
 * apply from starting on a pair that is not published yet. */
static volatile bool s_calibrated;
static double s_offset[3], s_scale[3];

/* Calibration sweep. Only the sampling task writes s_cal, and only while
 * s_cal_active, so the cal_* calls own it once they have cleared the flag. */
static compass_cal_t s_cal;
static volatile bool s_cal_active;
static int64_t s_cal_start_us;   /* sweep start, for the CAL_MAX_MS budget */

/* Enum settings are stored as the choice string (core/settings.h has no index
 * getter), and for all four below the index into the choices array IS the register
 * field value, so one lookup serves them all. The fallback is the schema default's
 * index: an unrecognised stored value means someone edited it by hand, and starting
 * the part in a documented configuration beats refusing to start. */
static int setting_enum_idx(settings_t *reg, const char *key,
                            const char *const *choices, int n, int fallback)
{
    char buf[16];
    settings_get_str(reg, key, buf, sizeof buf);
    for (int i = 0; i < n; i++)
        if (strcmp(choices[i], buf) == 0) return i;
    return fallback;
}

/* --- persistence (NVS "compass"/"magcal") -------------------------------- */

#define CAL_BLOB_MAGIC 0x31434d43u   /* "CMC1" */
#define CAL_BLOB_VER   1

typedef struct {          /* layout frozen by CMC1 */
    double offset[3];     /* hard-iron centre, uT */
    double scale[3];      /* soft-iron per-axis scale */
} cal_rec_t;

#define CAL_BLOB_LEN (5 + sizeof(cal_rec_t))   /* magic + version + record */

static void cal_store_save(const double offset[3], const double scale[3])
{
    uint8_t buf[CAL_BLOB_LEN];
    buf[0] = (uint8_t)CAL_BLOB_MAGIC;         buf[1] = (uint8_t)(CAL_BLOB_MAGIC >> 8);
    buf[2] = (uint8_t)(CAL_BLOB_MAGIC >> 16); buf[3] = (uint8_t)(CAL_BLOB_MAGIC >> 24);
    buf[4] = CAL_BLOB_VER;

    cal_rec_t r;
    memcpy(r.offset, offset, sizeof r.offset);
    memcpy(r.scale, scale, sizeof r.scale);
    memcpy(buf + 5, &r, sizeof r);

    nvs_handle_t h;
    if (nvs_open("compass", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "magcal", buf, sizeof buf);
        nvs_commit(h);
        nvs_close(h);
    }
}

static bool cal_store_load(double offset[3], double scale[3])
{
    nvs_handle_t h;
    if (nvs_open("compass", NVS_READONLY, &h) != ESP_OK) return false;
    size_t blob = 0;
    if (nvs_get_blob(h, "magcal", NULL, &blob) != ESP_OK || blob != CAL_BLOB_LEN) {
        nvs_close(h);
        return false;
    }
    uint8_t buf[CAL_BLOB_LEN];
    if (nvs_get_blob(h, "magcal", buf, &blob) != ESP_OK) { nvs_close(h); return false; }
    nvs_close(h);

    uint32_t magic = buf[0] | (buf[1] << 8) | (buf[2] << 16) | ((uint32_t)buf[3] << 24);
    if (magic != CAL_BLOB_MAGIC || buf[4] != CAL_BLOB_VER) return false;
    cal_rec_t r;
    memcpy(&r, buf + 5, sizeof r);
    for (int i = 0; i < 3; i++)
        if (!(r.scale[i] > 0.0)) return false;   /* a zero scale would erase the field */
    memcpy(offset, r.offset, sizeof r.offset);
    memcpy(scale, r.scale, sizeof r.scale);
    return true;
}

/* Publish a correction for the sampling task. No mutex, because every caller
 * runs at a higher priority on the sampler's own core (see s_calibrated above),
 * so the two memcpys cannot be interrupted by a read of what they are writing. */
static void cal_use(const double offset[3], const double scale[3])
{
    s_calibrated = false;
    memcpy(s_offset, offset, sizeof s_offset);
    memcpy(s_scale, scale, sizeof s_scale);
    s_calibrated = true;
}

/* --- sampling ------------------------------------------------------------- */

/* Declination and the calibration switch are user-visible, so they are re-read
 * at ~1 Hz (as mesh_svc does) rather than once at boot: a Settings change takes
 * effect without a reboot, and NVS is not touched at the sample rate. */
static void settings_refresh(void)
{
    if (!s_reg) return;
    s_decl_deg = (double)settings_get_int(s_reg, "mag_decl_ddeg") / 10.0;   /* stored in 0.1 deg */
    s_use_cal = settings_get_bool(s_reg, "mag_cal_use");
}

static void compass_task(void *arg)
{
    (void)arg;
    int since_refresh = 0;
    while (s_running) {
        /* The self-test changes the measurement mode, so it has to run in the task
         * that owns imu_read() rather than straight from the key handler. The
         * request is a single bool set by another task and cleared here, which
         * needs no lock: a request that lands during this check is served on the
         * next pass 50 ms later. */
        if (s_selftest_req) {
            s_selftest_req = false;
            imu_mag_selftest(&s_selftest);
            s_selftest_done = true;
        }

        imu_sample_t s;
        if (!imu_read(&s)) {
            s_errors++;
        } else {
            /* Attitude comes from gravity alone, so it is published on every
             * good read instead of only alongside a heading. A live roll and
             * pitch with no heading is what tells the two dies apart: the
             * accelerometer is answering and the magnetometer is not. */
            compass_attitude_deg(s.ax, s.ay, s.az, &s_reading.roll_deg, &s_reading.pitch_deg);
            s_last_tilt_us = esp_timer_get_time();

            /* The accelerometer above is the ICM's whatever happens here; only the
             * field is switchable. A separate part is read on its own, and the
             * sample's mag_ok is replaced by whether that read succeeded, so
             * everything downstream stays identical for both sources. */
            if (s_mag_source == MAG_SOURCE_QMC5883L)
                s.mag_ok = qmc5883l_read(&s.mx, &s.my, &s.mz);

            double mx = s.mx, my = s.my, mz = s.mz;
            /* The sweep accumulates the raw field: the correction is derived
             * from uncorrected extremes. Only a sample the fusion would trust is
             * folded in. A dropped, not-ready or saturated magnetometer read
             * arrives as zeroes with mag_ok false (see drivers/imu.h), and since
             * the box only ever widens, one of those would drag the centre off
             * by half the field for the rest of the sweep and make an inadequate
             * sweep pass the span gate as well. */
            if (s_cal_active && s.mag_ok) compass_cal_add(&s_cal, mx, my, mz);
            bool corrected = s_calibrated && s_use_cal;
            if (corrected) compass_cal_apply(s_offset, s_scale, &mx, &my, &mz);

            /* Two different questions, so two different checks. The RAW field
             * judges the sensor: it may carry hard iron, but it cannot be a
             * hundred times the earth's field. The CORRECTED field judges the
             * stored calibration, because a wrong correction makes a healthy
             * sensor look broken, and that would send the user out to buy a part
             * when a fresh sweep is what they need. */
            if (s.mag_ok) {
                s_field_raw_ut = sqrt(s.mx * s.mx + s.my * s.my + s.mz * s.mz);
                s_field_ut = sqrt(mx * mx + my * my + mz * mz);
                s_field_bad = !compass_raw_plausible(s.mx, s.my, s.mz);
                s_cal_bad = !s_field_bad && corrected && !compass_field_plausible(mx, my, mz);
                if (s_field_bad || s_cal_bad) {
                    s_implausible++;
                    if (esp_timer_get_time() - s_bad_log_us > 10 * 1000 * 1000) {
                        s_bad_log_us = esp_timer_get_time();
                        if (s_field_bad)
                            ESP_LOGW(TAG, "magnetometer reads %.0f uT raw: not a field "
                                          "(earth is 25-65 uT), heading suppressed",
                                     s_field_raw_ut);
                        else
                            ESP_LOGW(TAG, "raw field %.0f uT is sane but the saved "
                                          "calibration turns it into %.0f uT: sweep again",
                                     s_field_raw_ut, s_field_ut);
                    }
                }
            }

            double mag;
            if (s.mag_ok && !s_field_bad && !s_cal_bad &&
                compass_heading_deg(s.ax, s.ay, s.az, mx, my, mz, &mag)) {
                s_reading.magnetic_deg = mag;
                s_reading.heading_deg =
                    compass_lpf_update(&s_lpf, compass_true_deg(mag, s_decl_deg), LPF_ALPHA);
                /* An uncorrected magnetometer can be tens of degrees out, so the
                 * heading is only offered as usable once a correction is in use;
                 * the diagnostic fields above keep updating during a sweep. */
                s_reading.calibrated = corrected;
                s_reading.valid = corrected;
                s_samples++;
                s_last_sample_us = esp_timer_get_time();
            }

        }

        /* Retire a stale snapshot here, not in compass_get(): the UI polls that
         * at 10 Hz and it must stay a plain copy-out. */
        if (s_reading.valid &&
            esp_timer_get_time() - s_last_sample_us > (int64_t)COMPASS_NO_DATA_MS * 1000)
            s_reading.valid = false;

        /* Abandon an over-long sweep. This has to be the task rather than the
         * app: closing the Compass app mid-sweep is legal (and an app-to-app
         * jump would cancel a sweep the user means to finish), so the clock is
         * the only thing that can end one nobody came back to. Dropping the flag
         * is enough for the UI to recover, since the status it polls then reads
         * cal_active false and cal_ready false. */
        if (s_cal_active &&
            esp_timer_get_time() - s_cal_start_us > (int64_t)CAL_MAX_MS * 1000) {
            s_cal_active = false;
            ESP_LOGW(TAG, "mag cal sweep abandoned after %d s (%u samples)",
                     CAL_MAX_MS / 1000, (unsigned)s_cal.samples);
        }

        if (++since_refresh >= REFRESH_EVERY) { settings_refresh(); since_refresh = 0; }
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
    }
    vTaskDelete(NULL);
}

void compass_svc_init(settings_t *reg)
{
    s_reg = reg;
    settings_register_many(reg, COMPASS_SCHEMA, (int)(sizeof COMPASS_SCHEMA / sizeof COMPASS_SCHEMA[0]));
    /* Captured before the early return so the status can still say "off in
     * Settings" when the service never starts. */
    s_enabled = settings_get_bool(reg, "imu_enabled");
    if (!s_enabled) return;
    settings_refresh();

    double offset[3], scale[3];
    if (cal_store_load(offset, scale)) {
        cal_use(offset, scale);
        /* Scales matter as much as offsets: a marginal sweep gives one axis a
         * small span, and scale = mean/span then amplifies that axis and its
         * noise, which shows up as a restless heading rather than a wrong one. */
        ESP_LOGI(TAG, "mag cal loaded: off %.1f %.1f %.1f uT, scale %.3f %.3f %.3f",
                 offset[0], offset[1], offset[2], scale[0], scale[1], scale[2]);
    }

    int sda = (int)settings_get_int(reg, "imu_sda_pin");
    int scl = (int)settings_get_int(reg, "imu_scl_pin");
    int addr = settings_get_bool(reg, "imu_addr_hi") ? IMU_I2C_ADDR_HI : IMU_I2C_ADDR;

    /* The ICM comes up either way. It carries the accelerometer, so without it
     * there is no tilt compensation and no heading regardless of which part
     * measures the field. */
    esp_err_t e = imu_init(sda, scl, addr);
    if (e != ESP_OK) {
        /* No IMU fitted is a normal badge, so this stays a warning and the task
         * is never started: running=false makes compass_state_from() report OFF.
         * The pins are named because a wrong pair looks exactly like a dead part. */
        ESP_LOGW(TAG, "no IMU on sda=%d scl=%d addr=0x%02X: %s",
                 sda, scl, addr, esp_err_to_name(e));
        return;
    }

    int src = setting_enum_idx(reg, "mag_source", MAG_SOURCE_CHOICES, 2, MAG_SOURCE_IMU);
    if (src == MAG_SOURCE_QMC5883L) {
        qmc_range_t range = (qmc_range_t)setting_enum_idx(reg, "qmc_range",
                                                         QMC_RANGE_CHOICES, 2, QMC_RANGE_8G);
        qmc_osr_t osr = (qmc_osr_t)setting_enum_idx(reg, "qmc_osr",
                                                    QMC_OSR_CHOICES, 4, QMC_OSR_512);
        qmc_odr_t odr = (qmc_odr_t)setting_enum_idx(reg, "qmc_odr",
                                                   QMC_ODR_CHOICES, 4, QMC_ODR_100HZ);
        /* Same pins as the ICM: the module parallels onto the SAO bus, and
         * drivers/i2c_bus.h is what lets both of them have it. */
        if (qmc5883l_init(sda, scl, range, osr, odr) == ESP_OK) {
            s_mag_source = MAG_SOURCE_QMC5883L;
        } else {
            /* Deliberately no fall back to the AK09916. The setting was changed to
             * get away from that part, so quietly going back to it would present
             * the fault the user is trying to escape as the new module's. */
            ESP_LOGW(TAG, "mag_source is qmc5883l but none answered: no heading "
                          "(set mag_source back to imu to use the AK09916)");
        }
    } else {
        s_mag_source = MAG_SOURCE_IMU;
    }

    compass_lpf_init(&s_lpf);
    s_running = true;
    xTaskCreatePinnedToCore(compass_task, "compass", 3072, NULL, 3, NULL, 1);
    ESP_LOGI(TAG, "compass on I2C%d sda=%d scl=%d: accel from ICM 0x%02X, field from %s, "
                  "decl %.1f deg",
             SAO_I2C_PORT, sda, scl, addr,
             s_mag_source == MAG_SOURCE_QMC5883L ? "QMC5883L 0x0D" : "AK09916",
             s_decl_deg);
}

bool compass_get(compass_reading_t *out) { *out = s_reading; return s_reading.valid; }

void compass_get_status(compass_status_t *out)
{
    int64_t now = esp_timer_get_time();
    imu_status_t imu;
    imu_get_status(&imu);
    out->enabled = s_enabled;
    out->running = s_running;
    out->present = imu.present;
    out->mag_present = imu.mag_present;
    out->cal_active = s_cal_active;
    out->cal_samples = s_cal.samples;
    out->cal_ready = s_cal_active && compass_cal_done(&s_cal);
    /* Two separate facts, because they need two different user actions: a stored
     * calibration with mag_cal_use off is still stored, and reporting it as
     * "None" would send the user on a sweep that changes nothing. */
    out->cal_stored = s_calibrated;
    out->cal_in_use = s_calibrated && s_use_cal;
    out->samples = s_samples;
    out->errors = s_errors;
    out->ms_since_sample = s_last_sample_us ? (uint32_t)((now - s_last_sample_us) / 1000) : UINT32_MAX;
    out->ms_since_tilt = s_last_tilt_us ? (uint32_t)((now - s_last_tilt_us) / 1000) : UINT32_MAX;
    /* Transport view straight from the driver: a valid WHO_AM_I with rising I2C
     * errors and no fused samples is the signature of a bad magnetometer joint. */
    out->imu_whoami = imu.whoami;
    out->imu_reads = imu.reads;
    out->imu_errors = imu.errors;
    out->declination_deg = s_decl_deg;
    out->reading = s_reading;
    /* Derived once here so every page agrees. A dead magnetometer die is NOT
     * folded into "not running": the task is sampling and the accelerometer is
     * answering, so it belongs in NO_DATA, where the pages can name the die that
     * failed. Folding it into OFF would blame the whole part for a fault in half
     * of it, which is the opposite of what someone debugging a fresh solder joint
     * needs to read. */
    out->implausible = s_implausible;
    out->field_ut = s_field_ut;
    out->field_raw_ut = s_field_raw_ut;
    /* A correction that produces an impossible field is not a usable calibration,
     * so the state says UNCAL and the UI asks for a sweep, rather than accusing
     * the sensor. */
    out->cal_bad = s_cal_bad;
    out->mag_source = s_mag_source;
    out->selftest_done = s_selftest_done;
    out->selftest = s_selftest;
    out->state = compass_state_from(s_running, out->ms_since_sample,
                                    out->reading.calibrated && !s_cal_bad, s_field_bad);
}

/* --- calibration ---------------------------------------------------------- */

void compass_selftest_begin(void)
{
    /* Only the AK09916 has a self-test coil, so a request while another part
     * supplies the field is dropped rather than quietly testing a magnetometer
     * that is not the one being used. */
    if (s_mag_source != MAG_SOURCE_IMU) return;
    s_selftest_done = false;
    s_selftest_req = true;
}

void compass_cal_begin(void)
{
    /* Nothing accumulates without the sampling task, and a sweep that can never
     * finish would leave cal_active stuck on in the status the UI polls. */
    if (!s_running) return;
    compass_cal_init(&s_cal);   /* seeded before the task is allowed to add */
    s_cal_start_us = esp_timer_get_time();   /* set before the flag: the task
                                              * must never see active with a
                                              * stale start and abandon at once */
    s_cal_active = true;
    ESP_LOGI(TAG, "mag cal sweep started");
}

bool compass_cal_save(void)
{
    if (!s_cal_active || !compass_cal_done(&s_cal)) return false;   /* keep sweeping */
    s_cal_active = false;   /* stops the adds before the extremes are read; a last
                             * in-flight sample can only widen the box, not break it */
    double offset[3], scale[3];
    if (!compass_cal_result(&s_cal, offset, scale)) return false;

    cal_store_save(offset, scale);
    cal_use(offset, scale);   /* the next sample is corrected: no reboot in the loop */
    ESP_LOGI(TAG, "mag cal saved: %u samples, off %.1f %.1f %.1f uT, scale %.3f %.3f %.3f",
             (unsigned)s_cal.samples, offset[0], offset[1], offset[2], scale[0], scale[1], scale[2]);
    return true;
}

void compass_cal_cancel(void)
{
    s_cal_active = false;
    ESP_LOGI(TAG, "mag cal sweep cancelled");
}
