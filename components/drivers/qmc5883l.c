/* See drivers/qmc5883l.h. QMC5883L over the new I2C master driver. */
#include "drivers/qmc5883l.h"
#include "drivers/i2c_bus.h"
#include "board_pins.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "qmc5883l";

/* Registers (QST QMC5883L datasheet rev B). Nothing here matches the HMC5883L the
 * boards are labelled as, which is exactly why a driver for the wrong part reads
 * nothing useful. */
#define R_DATA_X_LSB   0x00   /* first of 6: X, Y, Z, little-endian pairs */
#define R_STATUS       0x06
#define R_TEMP_LSB     0x07
#define R_CTRL1        0x09
#define R_CTRL2        0x0A
#define R_SETRESET     0x0B
#define R_CHIP_ID      0x0D

#define ST_DRDY        0x01
#define ST_OVL         0x02   /* the field exceeded the selected range */
#define ST_DOR         0x04   /* a sample was produced before the last was read */

#define CTRL1_MODE_CONT   0x01   /* bits 1:0; 00 is standby */
#define CTRL2_SOFT_RST    0x80

/* The datasheet gives one value for this register and no alternative. Leaving it
 * alone is a documented way to get a part that answers and misbehaves. */
#define SETRESET_VALUE    0x01

#define CHIP_ID_QMC5883L  0xFF

#define QMC_I2C_ADDR      0x0D

/* Counts to microtesla. 1 gauss is 100 uT, so 3000 LSB/G at 8 G is 30 counts per
 * uT and 12000 LSB/G at 2 G is 120. */
#define UT_PER_LSB_8G     (1.0 / 30.0)
#define UT_PER_LSB_2G     (1.0 / 120.0)

static i2c_master_dev_handle_t s_dev;
static bool s_present;
static double s_ut_per_lsb = UT_PER_LSB_8G;
static uint8_t s_chip_id;
static uint32_t s_reads, s_errors, s_not_ready, s_overflows, s_skipped;

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, 2, 50);
}

static esp_err_t reg_read(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 50);
}

static int16_t le16(const uint8_t *p) { return (int16_t)((p[1] << 8) | p[0]); }

esp_err_t qmc5883l_init(int sda_pin, int scl_pin,
                        qmc_range_t range, qmc_osr_t osr, qmc_odr_t odr)
{
    s_present = false;

    i2c_master_bus_handle_t bus;
    esp_err_t e = i2c_bus_get(SAO_I2C_PORT, sda_pin, scl_pin, &bus);
    if (e != ESP_OK) return e;

    if (!s_dev) {
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = QMC_I2C_ADDR,
            .scl_speed_hz = SAO_I2C_HZ,
        };
        e = i2c_master_bus_add_device(bus, &cfg, &s_dev);
        if (e != ESP_OK) { ESP_LOGE(TAG, "i2c dev: %s", esp_err_to_name(e)); return e; }
    }

    /* Presence is the address acknowledging, NOT the chip id, because this part's
     * id register reads 0xFF and that is also what a floating bus reads. Checking
     * the id alone would report a missing module as a healthy one. */
    if (i2c_master_probe(bus, QMC_I2C_ADDR, 100) != ESP_OK) {
        ESP_LOGW(TAG, "nothing acknowledged at 0x%02X on sda=%d scl=%d",
                 QMC_I2C_ADDR, sda_pin, scl_pin);
        return ESP_ERR_NOT_FOUND;
    }

    s_chip_id = 0;
    if (reg_read(R_CHIP_ID, &s_chip_id, 1) != ESP_OK) {
        ESP_LOGW(TAG, "0x%02X acknowledged but the id register did not read", QMC_I2C_ADDR);
        return ESP_ERR_NOT_FOUND;
    }
    if (s_chip_id != CHIP_ID_QMC5883L) {
        /* A QMC5883P is in circulation with a different register map, and refusing
         * here is better than configuring it as if it were an L and reporting the
         * result as a field. */
        ESP_LOGW(TAG, "0x%02X answered with chip id 0x%02X, expected 0x%02X: not a QMC5883L "
                      "(a QMC5883P has its own register map), so it is not used",
                 QMC_I2C_ADDR, s_chip_id, CHIP_ID_QMC5883L);
        return ESP_ERR_NOT_FOUND;
    }

    /* Reset before configuring, never after: a soft reset drops MODE back to
     * standby, so a reset issued last would silently stop the part. */
    if (reg_write(R_CTRL2, CTRL2_SOFT_RST) != ESP_OK) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(10));
    reg_write(R_SETRESET, SETRESET_VALUE);

    uint8_t ctrl1 = (uint8_t)(((uint8_t)osr << 6) | ((uint8_t)range << 4) |
                              ((uint8_t)odr << 2) | CTRL1_MODE_CONT);
    if (reg_write(R_CTRL1, ctrl1) != ESP_OK) return ESP_FAIL;

    /* Read it back. A part that accepts a write and reports something else is the
     * failure mode that cost days on the AK09916, so it is checked here from the
     * start rather than added after the fact. */
    uint8_t back = 0;
    if (reg_read(R_CTRL1, &back, 1) != ESP_OK || back != ctrl1) {
        ESP_LOGW(TAG, "CTRL1 wrote 0x%02X and reads 0x%02X", ctrl1, back);
        return ESP_FAIL;
    }

    s_ut_per_lsb = (range == QMC_RANGE_8G) ? UT_PER_LSB_8G : UT_PER_LSB_2G;
    s_present = true;

    static const int odr_hz[] = { 10, 50, 100, 200 };
    static const int osr_n[]  = { 512, 256, 128, 64 };
    ESP_LOGI(TAG, "QMC5883L up at 0x%02X: %d G, OSR %d, %d Hz",
             QMC_I2C_ADDR, range == QMC_RANGE_8G ? 8 : 2,
             osr_n[osr], odr_hz[odr]);
    return ESP_OK;
}

bool qmc5883l_read(double *mx, double *my, double *mz)
{
    if (!s_present) return false;

    /* Status first, then the block, so DRDY is known before the data is trusted.
     * Seven bytes in one transfer would be cheaper but would read the status that
     * belongs to the sample after this one. */
    uint8_t st = 0;
    if (reg_read(R_STATUS, &st, 1) != ESP_OK) { s_errors++; return false; }

    /* Counted, not rejected. DOR only means a sample was produced faster than it
     * was collected, which is expected at 100 Hz with a 20 Hz consumer, and the
     * data itself is still the most recent complete measurement. */
    if (st & ST_DOR) s_skipped++;
    if (!(st & ST_DRDY)) { s_not_ready++; return false; }

    /* OVL means the field is off the top of the selected range, so the counts are
     * clipped and the sample is not a measurement. On the 8 G range that would
     * mean over 800 uT, which is worth counting loudly rather than averaging in. */
    if (st & ST_OVL) { s_overflows++; return false; }

    uint8_t d[6];
    if (reg_read(R_DATA_X_LSB, d, sizeof d) != ESP_OK) { s_errors++; return false; }

    /* Sensor frame, no rotation applied. The heading fuses this with the ICM's
     * accelerometer, so the two have to share a frame, and the transform depends on
     * how the module ends up physically mounted. Determine it once with the badge
     * flat, reading the Diagnostics field row while turning the badge through
     * north, east, south and west, then apply the swap and sign changes here. */
    *mx = le16(&d[0]) * s_ut_per_lsb;
    *my = le16(&d[2]) * s_ut_per_lsb;
    *mz = le16(&d[4]) * s_ut_per_lsb;
    s_reads++;
    return true;
}

void qmc5883l_get_status(qmc5883l_status_t *out)
{
    out->present = s_present;
    out->chip_id = s_chip_id;
    out->reads = s_reads;
    out->errors = s_errors;
    out->not_ready = s_not_ready;
    out->overflows = s_overflows;
    out->skipped = s_skipped;
}
