/* See drivers/imu.h. ICM-20948 over the new I2C master driver; the AK09916
 * magnetometer through the ICM's auxiliary I2C master, not through bypass. */
#include "drivers/imu.h"
#include "drivers/i2c_bus.h"
#include "board_pins.h"

#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"

static const char *TAG = "imu";

/* ICM-20948 registers (DS-000189 rev 1.3, sections 7.1 and 7.3). The bank is
 * part of each name because the same address means something different in each
 * bank, and a wrong bank reads plausible garbage instead of failing. */
#define REG_BANK_SEL       0x7F   /* present in every bank */
#define B0_WHO_AM_I        0x00
#define B0_USER_CTRL       0x03
#define B0_PWR_MGMT_1      0x06
#define B0_PWR_MGMT_2      0x07
#define B0_LP_CONFIG       0x05   /* resets to 0x40: I2C_MST_CYCLE set */
#define B0_INT_PIN_CFG     0x0F
#define B0_ACCEL_XOUT_H    0x2D   /* first of 14: accel, gyro, temperature */
#define B2_GYRO_CONFIG_1   0x01
#define B2_ACCEL_CONFIG    0x14
#define B0_EXT_SLV_DATA_00 0x3B   /* where the aux master parks what it read */
#define B0_I2C_MST_STATUS  0x17   /* SLV4_DONE is bit 6, SLV4_NACK bit 4 */
#define B3_I2C_SLV4_ADDR   0x13   /* the one-shot channel: address, register, */
#define B3_I2C_SLV4_REG    0x14   /* control, data-out, data-in */
#define B3_I2C_SLV4_CTRL   0x15
#define B3_I2C_SLV4_DO     0x16
#define B3_I2C_SLV4_DI     0x17
#define MST_SLV4_DONE      0x40
#define MST_SLV4_NACK      0x10
#define SLV4_EN            0x80
#define USER_CTRL_MST_RST  0x02   /* I2C_MST_RST, clears a wedged aux master */
#define LP_CONFIG_MST_CYCLE 0x40  /* reset value: the aux master polls on a duty cycle */
#define B3_I2C_MST_ODR_CFG 0x00   /* aux master repeat rate: 1.1 kHz >> this */
#define B3_I2C_MST_CTRL    0x01
#define B3_I2C_SLV0_ADDR   0x03
#define B3_I2C_SLV0_REG    0x04
#define B3_I2C_SLV0_CTRL   0x05
#define USER_CTRL_MST_ON   0x20   /* I2C_MST_EN */
#define MST_CTRL_345KHZ    0x17   /* 345.6 kHz + P_NSR stop-between-reads */
#define MST_ODR_69HZ       0x04   /* 1.1 kHz / 2^4, just under the mag's 100 Hz */
#define SLV0_READ_FLAG     0x80   /* OR into the address for a read transfer */
#define SLV0_EN            0x80   /* OR into CTRL with the byte count */

#define WHOAMI_ICM20948    0xEA   /* section 8.1 */
#define MPU_WHO_AM_I       0x75   /* where the MPU-6050/9250 family keeps its ID */
#define PWR1_DEVICE_RESET  0x80   /* section 8.4, self-clearing */
#define PWR1_CLKSEL_AUTO   0x01   /* CLKSEL 1..5 = PLL when ready, else internal */
#define PWR2_ALL_ON        0x00   /* section 8.5, DISABLE_ACCEL/GYRO both 000 */
#define GYRO_CFG_250DPS    0x31   /* DLPFCFG 6 (5.7 Hz), FS_SEL 0, FCHOICE 1 */
#define ACCEL_CFG_2G       0x31   /* DLPFCFG 6 (5.7 Hz), FS_SEL 0, FCHOICE 1 */
#define AG_BURST           14

/* AK09916 magnetometer: a second die with its own I2C address, wired to the
 * ICM's auxiliary pins (DS-000189 sections 12 and 13, AK09916 datasheet). */
#define MAG_I2C_ADDR       0x0C   /* fixed in the package, no strap to read */
#define M_WIA1             0x00   /* company id */
#define M_WIA2             0x01   /* device id */
#define M_ST1              0x10   /* first of 9: ST1, HXL..HZH, dummy, ST2 */
#define M_CNTL2            0x31
#define M_CNTL3            0x32
#define M_WIA1_AKM         0x48
#define M_WIA2_AK09916     0x09
#define M_CNTL2_POWERDOWN  0x00
#define M_CNTL2_CONT100HZ  0x08   /* continuous measurement mode 4 */
#define M_CNTL2_SELFTEST   0x10   /* one measurement of the on-die coil */
#define M_CNTL3_SRST       0x01
#define M_ST1_DRDY         0x01
#define M_ST2_HOFL         0x08   /* sensor saturated: the sample is not a field */
#define MAG_BURST          9

/* Counts to physical units at the full scales selected in imu_init (DS-000189
 * section 3; the magnetometer figure is the same in the AK09916 datasheet). */
#define ACCEL_LSB_PER_G    16384.0   /* ACCEL_FS_SEL 0, table 2 */
#define GYRO_LSB_PER_DPS   131.0     /* GYRO_FS_SEL 0, table 1 */
#define MAG_UT_PER_LSB     0.15      /* table 3 */
#define TEMP_LSB_PER_C     333.87    /* table 5 */
#define TEMP_OFFSET_C      21.0      /* the temperature at which the offset is 0 LSB */

static i2c_master_dev_handle_t s_dev;
static bool s_present, s_mag_present;
static uint8_t s_whoami;
static uint32_t s_reads, s_errors;

static esp_err_t reg_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(dev, b, 2, 50);
}
static esp_err_t reg_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(dev, &reg, 1, buf, n, 50);
}

/* USER_BANK is bits 5:4, so the bank number shifts up (section 8.65). */
static esp_err_t bank_sel(uint8_t b) { return reg_write(s_dev, REG_BANK_SEL, (uint8_t)(b << 4)); }

/* Nothing answered as an ICM-20948: report what IS on the bus rather than leave
 * the user guessing between a dead part, a wrong address and a different chip.
 * The MPU-6050/9250 family shares these addresses but keeps WHO_AM_I at 0x75,
 * and its register 0x00 is a factory self-test byte, so a small odd value there
 * is the signature of one of those rather than of a broken ICM. */
static void report_bus(i2c_master_bus_handle_t bus)
{
    char found[48];
    int n = 0;
    for (int a = 0x08; a <= 0x77 && n < (int)sizeof found - 6; a++) {
        if (i2c_master_probe(bus, (uint16_t)a, 50) != ESP_OK) continue;
        n += snprintf(found + n, sizeof found - (size_t)n, " 0x%02X", a);
    }
    ESP_LOGW(TAG, "i2c scan:%s", n ? found : " nothing answered");

    uint8_t who = 0;
    if (reg_read(s_dev, MPU_WHO_AM_I, &who, 1) == ESP_OK && who != 0x00)
        ESP_LOGW(TAG, "reg 0x75 reads 0x%02X: an MPU-family part, not an ICM-20948"
                      " (0x68 MPU-6050, 0x70 MPU-6500, 0x71 MPU-9250, 0x73 MPU-9255)", who);
}

/* --- reaching the AK09916 through the ICM's auxiliary master ----------------
 * The magnetometer sits on a private I2C bus behind the ICM, so every access is
 * a transaction the ICM performs on our behalf. Two channels are used, for two
 * different jobs:
 *
 *   SLV4 is a one-shot: write address, register and (for a write) the byte, then
 *   poll SLV4_DONE. Configuration only, because it costs a round trip per byte.
 *
 *   SLV0 is a standing order: once armed it re-reads the same block on the ICM's
 *   own schedule and drops it in EXT_SLV_SENS_DATA_00, so imu_read() picks up
 *   nine bytes from the ICM with no aux traffic of its own.
 *
 * I2C_MST_CYCLE in LP_CONFIG is what drives that schedule, so it is left at its
 * reset value. Clearing it stops the standing order dead: the ICM keeps
 * answering, EXT_SLV_SENS_DATA stays all zeroes, and I2C_MST_STATUS reports no
 * error because from the master's point of view nothing was ever asked of it. */

#define SLV4_TIMEOUT_MS 20   /* one aux transaction at 345 kHz is tens of us */

static esp_err_t slv4_txn(uint8_t reg, uint8_t *val, bool read)
{
    bank_sel(3);
    reg_write(s_dev, B3_I2C_SLV4_ADDR,
              (uint8_t)(read ? (SLV0_READ_FLAG | MAG_I2C_ADDR) : MAG_I2C_ADDR));
    reg_write(s_dev, B3_I2C_SLV4_REG, reg);
    if (!read) reg_write(s_dev, B3_I2C_SLV4_DO, *val);
    /* EN only. DLY 0, no INT, REG_DIS clear so the register address is sent. */
    reg_write(s_dev, B3_I2C_SLV4_CTRL, SLV4_EN);

    bank_sel(0);
    uint8_t st = 0;
    for (int waited = 0; waited < SLV4_TIMEOUT_MS; waited++) {
        if (reg_read(s_dev, B0_I2C_MST_STATUS, &st, 1) != ESP_OK) return ESP_FAIL;
        if (st & MST_SLV4_DONE) break;
        vTaskDelay(1);
    }
    if (!(st & MST_SLV4_DONE)) return ESP_ERR_TIMEOUT;
    /* A NACK means the aux bus answered nothing, which is a different fault from
     * a transaction that never ran, and only this bit separates them. */
    if (st & MST_SLV4_NACK) return ESP_ERR_NOT_FOUND;

    if (read) {
        bank_sel(3);
        esp_err_t e = reg_read(s_dev, B3_I2C_SLV4_DI, val, 1);
        bank_sel(0);
        return e;
    }
    return ESP_OK;
}

static esp_err_t mag_write(uint8_t reg, uint8_t val) { return slv4_txn(reg, &val, false); }
static esp_err_t mag_read(uint8_t reg, uint8_t *val) { return slv4_txn(reg, val, true); }

/* Arm or disarm the standing order that keeps EXT_SLV_SENS_DATA fed. */
static void mag_stream(bool on)
{
    bank_sel(3);
    reg_write(s_dev, B3_I2C_SLV0_ADDR, (uint8_t)(SLV0_READ_FLAG | MAG_I2C_ADDR));
    reg_write(s_dev, B3_I2C_SLV0_REG, M_ST1);
    reg_write(s_dev, B3_I2C_SLV0_CTRL, on ? (uint8_t)(SLV0_EN | MAG_BURST) : 0x00);
    bank_sel(0);
}

/* Accel, gyro and temperature are big-endian; the magnetometer is little-endian. */
static int16_t be16(const uint8_t *p) { return (int16_t)((p[0] << 8) | p[1]); }
static int16_t le16(const uint8_t *p) { return (int16_t)((p[1] << 8) | p[0]); }

esp_err_t imu_init(int sda_pin, int scl_pin, int addr)
{
    /* Shared, because a separate magnetometer can sit on these same two pins and
     * only one driver may create the bus (see drivers/i2c_bus.h). */
    i2c_master_bus_handle_t bus;
    esp_err_t e = i2c_bus_get(SAO_I2C_PORT, sda_pin, scl_pin, &bus);
    if (e != ESP_OK) return e;

    /* Kept because dev_cfg is reused below for the magnetometer, and a log line
     * that names the magnetometer's address while reporting the IMU is worse
     * than no log line at all. */
    const uint16_t icm_addr = addr > 0 ? (uint16_t)addr : IMU_I2C_ADDR;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = icm_addr,
        .scl_speed_hz = SAO_I2C_HZ,
    };
    e = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (e != ESP_OK) { ESP_LOGE(TAG, "i2c dev: %s", esp_err_to_name(e)); return e; }

    /* The magnetometer is deliberately NOT added as a device on this bus. It could
     * be, through BYPASS_EN, and that path reads its identity and control
     * registers perfectly -- which is exactly what makes it a trap. On this badge
     * every measurement that came back through bypass was noise, while WIA, CNTL2
     * and CNTL3 all read back correctly, and the AK09916's own self-test failed
     * against a field generated on its own die. Three modules from two suppliers
     * behaved identically, so the part was not the problem: the access path was.
     * Every working driver for this package (SparkFun, Pimoroni, InvenSense's own)
     * reaches the AK09916 through the ICM's auxiliary master instead, which is
     * what the code below does. */

    /* Bank discipline: every block below names its bank before touching a
     * register, and init leaves bank 0 selected. imu_read() then reads only
     * bank 0 and never switches, so the steady-state path cannot drift. A stale
     * bank from earlier firmware would otherwise make WHO_AM_I read
     * GYRO_SMPLRT_DIV and look like a missing chip. */
    bank_sel(0);
    /* Distinguish silence from a wrong answer: the byte a failed transfer leaves
     * in the buffer is not defined, so reporting it as an ID sends people
     * hunting for a chip that reads 0x17 when in fact nothing acknowledged. */
    esp_err_t rd = reg_read(s_dev, B0_WHO_AM_I, &s_whoami, 1);
    if (rd != ESP_OK) s_whoami = 0;
    if (rd != ESP_OK || s_whoami != WHOAMI_ICM20948) {
        if (rd != ESP_OK)
            ESP_LOGW(TAG, "nothing acknowledged at 0x%02X on sda=%d scl=%d: %s",
                     icm_addr, sda_pin, scl_pin, esp_err_to_name(rd));
        else
            ESP_LOGW(TAG, "wrong part at 0x%02X: WHO_AM_I 0x%02X, expected 0x%02X",
                     icm_addr, s_whoami, WHOAMI_ICM20948);
        report_bus(bus);
        return ESP_ERR_NOT_FOUND;
    }

    /* The part boots asleep (PWR_MGMT_1 resets to 0x41), so reset then wake.
     * 100 ms is the datasheet's worst-case register start-up time (table 5),
     * which is cheaper to wait out once than to poll for. */
    reg_write(s_dev, B0_PWR_MGMT_1, PWR1_DEVICE_RESET);
    vTaskDelay(pdMS_TO_TICKS(100));
    reg_write(s_dev, B0_PWR_MGMT_1, PWR1_CLKSEL_AUTO);
    reg_write(s_dev, B0_PWR_MGMT_2, PWR2_ALL_ON);

    /* Finest full scale, because a tilt compass measures gravity rather than
     * motion, and the narrowest low-pass, because the service samples at 20 Hz
     * and hand tremor left in a 1.2 kHz band aliases down into the heading. */
    bank_sel(2);
    reg_write(s_dev, B2_GYRO_CONFIG_1, GYRO_CFG_250DPS);
    reg_write(s_dev, B2_ACCEL_CONFIG, ACCEL_CFG_2G);
    bank_sel(0);

    /* BYPASS_EN and the aux master are mutually exclusive (section 8.6), and this
     * driver wants the master: clear bypass first so the aux pins come back under
     * the ICM's control, then reset the master in case earlier firmware left a
     * transaction half finished. */
    reg_write(s_dev, B0_INT_PIN_CFG, 0x00);
    reg_write(s_dev, B0_USER_CTRL, USER_CTRL_MST_RST);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* I2C_MST_CYCLE stays set. It is the reset value and it is what makes the
     * master poll SLV0 at all; clearing it leaves the shadow buffer permanently
     * zero with no error reported anywhere. */
    reg_write(s_dev, B0_LP_CONFIG, LP_CONFIG_MST_CYCLE);
    bank_sel(3);
    reg_write(s_dev, B3_I2C_MST_CTRL, MST_CTRL_345KHZ);
    /* How often the master repeats the standing order: 1.1 kHz >> ODR. The reset
     * value of 0 polls a 100 Hz magnetometer eleven times per measurement, and
     * every one of those reads ST2, which is the register that tells the die its
     * data has been consumed. Measured spread across this setting on real hardware:
     * 440 uT at 1.1 kHz against 200 uT at 69 Hz, so the over-polling was corrupting
     * roughly half the reading. 69 Hz still delivers three samples per 20 Hz poll. */
    reg_write(s_dev, B3_I2C_MST_ODR_CFG, MST_ODR_69HZ);
    bank_sel(0);
    reg_write(s_dev, B0_USER_CTRL, USER_CTRL_MST_ON);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Identity over the aux path, retrying through a master reset. The first
     * transaction after enabling the master is the one that fails if it was left
     * wedged, and a single retry turns that from a dead compass into a log line. */
    uint8_t id[2] = { 0, 0 };
    for (int try = 0; try < 3; try++) {
        if (mag_read(M_WIA1, &id[0]) == ESP_OK && mag_read(M_WIA2, &id[1]) == ESP_OK &&
            id[0] == M_WIA1_AKM && id[1] == M_WIA2_AK09916) {
            s_mag_present = true;
            break;
        }
        reg_write(s_dev, B0_USER_CTRL, USER_CTRL_MST_RST);
        vTaskDelay(pdMS_TO_TICKS(10));
        reg_write(s_dev, B0_USER_CTRL, USER_CTRL_MST_ON);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (!s_mag_present) {
        /* Accel and gyro still give tilt, so keep going without a heading. */
        ESP_LOGW(TAG, "no AK09916 over the aux master (id 0x%02X%02X): no heading",
                 id[0], id[1]);
    } else {
        /* A mode change has to pass through power-down (AK09916 section 9.3),
         * Twait is 100 us, and one tick is the shortest wait available. 100 Hz is
         * the only continuous rate that always has a fresh sample ready for a
         * 20 Hz poller: at 10 Hz half the reads would find DRDY clear. */
        mag_write(M_CNTL3, M_CNTL3_SRST);
        vTaskDelay(pdMS_TO_TICKS(2));
        mag_write(M_CNTL2, M_CNTL2_POWERDOWN);
        vTaskDelay(pdMS_TO_TICKS(2));
        mag_write(M_CNTL2, M_CNTL2_CONT100HZ);
        mag_stream(true);
        vTaskDelay(pdMS_TO_TICKS(20));   /* let the first block land */
    }

    s_present = true;

    ESP_LOGI(TAG, "ICM-20948 up (id 0x%02X), magnetometer %s",
             s_whoami, s_mag_present ? "streaming at 100 Hz via aux master" : "absent");
    return ESP_OK;
}

bool imu_read(imu_sample_t *out)
{
    if (!s_present) return false;

    uint8_t b[AG_BURST];
    if (reg_read(s_dev, B0_ACCEL_XOUT_H, b, sizeof b) != ESP_OK) { s_errors++; return false; }
    s_reads++;

    out->ax = be16(&b[0]) / ACCEL_LSB_PER_G;
    out->ay = be16(&b[2]) / ACCEL_LSB_PER_G;
    out->az = be16(&b[4]) / ACCEL_LSB_PER_G;
    out->gx = be16(&b[6]) / GYRO_LSB_PER_DPS;
    out->gy = be16(&b[8]) / GYRO_LSB_PER_DPS;
    out->gz = be16(&b[10]) / GYRO_LSB_PER_DPS;
    out->temp_c = be16(&b[12]) / TEMP_LSB_PER_C + TEMP_OFFSET_C;

    /* A magnetometer that is absent, quiet or saturated is not a transport
     * failure: the tilt above is still good, so report it with mag_ok clear
     * rather than throwing the whole sample away. */
    out->mx = out->my = out->mz = 0.0;
    out->mag_ok = false;
    if (!s_mag_present) return true;

    /* One read of the ICM's shadow buffer, not of the magnetometer. The aux master
     * has already fetched ST1 through ST2 as one block on its own schedule, so
     * this is a plain nine-byte read from the ICM and the block is internally
     * consistent: every byte came from the same aux transaction, which is what
     * reading the die directly could not guarantee. */
    uint8_t m[MAG_BURST];
    if (reg_read(s_dev, B0_EXT_SLV_DATA_00, m, sizeof m) != ESP_OK) {
        s_errors++;
        return true;
    }
    if (!(m[0] & M_ST1_DRDY)) return true;
    if (m[8] & M_ST2_HOFL) return true;      /* saturated: not a field */

    out->mx =  le16(&m[1]) * MAG_UT_PER_LSB;
    out->my = -le16(&m[3]) * MAG_UT_PER_LSB;
    out->mz = -le16(&m[5]) * MAG_UT_PER_LSB;
    out->mag_ok = true;
    return true;
}

/* The AK09916 can measure a field generated by a coil on its own die, so the
 * expected answer is fixed by the datasheet instead of by where the badge is
 * standing. That makes this the only test that separates a broken magnetometer
 * from a magnetic environment, a wiring fault or a bad calibration -- all four
 * look identical from the outside, and three of them are the user's problem
 * while the fourth is not. It exists because working that out by elimination
 * took days; it now takes a keypress.
 *
 * Counts, not uT, because the windows in table 6 are given in counts. Repeated
 * because a healthy die returns nearly the same numbers every time: a single
 * pass proves less than five consistent ones, and a die returning noise
 * occasionally lands inside the window by luck. */
#define ST_REPEATS 5
#define ST_XY_ABS  200    /* table 6: X and Y within +/-200 counts */
#define ST_Z_MIN  (-1000) /* table 6: Z between -1000 and -200 counts */
#define ST_Z_MAX  (-200)

bool imu_mag_selftest(imu_mag_selftest_t *out)
{
    *out = (imu_mag_selftest_t){ 0 };
    if (!s_present || !s_mag_present) return false;

    /* The standing order has to stop for the duration: it reads ST2 on every ICM
     * sample, and that read is what tells the die to release the next
     * measurement, so leaving it armed would consume the self-test result before
     * this function could read it. */
    mag_stream(false);
    vTaskDelay(pdMS_TO_TICKS(20));

    mag_write(M_CNTL3, M_CNTL3_SRST);
    vTaskDelay(pdMS_TO_TICKS(2));
    uint8_t id[2] = { 0, 0 };
    out->id_ok = (mag_read(M_WIA1, &id[0]) == ESP_OK && mag_read(M_WIA2, &id[1]) == ESP_OK &&
                  id[0] == M_WIA1_AKM && id[1] == M_WIA2_AK09916);

    for (int i = 0; i < ST_REPEATS; i++) {
        mag_write(M_CNTL2, M_CNTL2_POWERDOWN);
        vTaskDelay(pdMS_TO_TICKS(2));
        if (mag_write(M_CNTL2, M_CNTL2_SELFTEST) != ESP_OK) break;

        /* Poll DRDY rather than assume a conversion time: a fixed delay that is
         * slightly short reads the previous contents and calls it a result. */
        uint8_t st1 = 0;
        int waited = 0;
        do {
            vTaskDelay(pdMS_TO_TICKS(2));
            waited += 2;
            if (mag_read(M_ST1, &st1) != ESP_OK) break;
        } while (!(st1 & M_ST1_DRDY) && waited < 200);
        if (!(st1 & M_ST1_DRDY)) continue;      /* no measurement, not a failed one */

        uint8_t d[MAG_BURST - 1];               /* HXL..ST2, one SLV4 round trip each */
        bool ok = true;
        for (unsigned k = 0; k < sizeof d; k++)
            if (mag_read((uint8_t)(M_ST1 + 1 + k), &d[k]) != ESP_OK) { ok = false; break; }
        if (!ok) break;

        out->runs++;
        out->x = le16(&d[0]);
        out->y = le16(&d[2]);
        out->z = le16(&d[4]);
        if (out->x >= -ST_XY_ABS && out->x <= ST_XY_ABS &&
            out->y >= -ST_XY_ABS && out->y <= ST_XY_ABS &&
            out->z >= ST_Z_MIN   && out->z <= ST_Z_MAX) out->passes++;

        ESP_LOGI(TAG, "mag self-test %d/%d: %6d %6d %6d counts", i + 1, ST_REPEATS,
                 out->x, out->y, out->z);
    }

    /* Back to the mode and the standing order the driver samples with, whatever
     * the verdict: a failed self-test must not also leave the compass dead. */
    mag_write(M_CNTL2, M_CNTL2_POWERDOWN);
    vTaskDelay(pdMS_TO_TICKS(2));
    mag_write(M_CNTL2, M_CNTL2_CONT100HZ);
    mag_stream(true);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Every repeat has to pass. A die that passes three of five is not a working
     * magnetometer with bad luck, it is one returning numbers that occasionally
     * land in the window. */
    out->ran  = (out->runs == ST_REPEATS);
    out->pass = out->ran && out->passes == ST_REPEATS && out->id_ok;
    ESP_LOGW(TAG, "mag self-test: %s (%d of %d runs inside the datasheet window)",
             out->pass ? "PASS" : "FAIL", out->passes, out->runs);
    return out->ran;
}

void imu_get_status(imu_status_t *out)
{
    out->present = s_present;
    out->mag_present = s_mag_present;
    out->whoami = s_whoami;
    out->reads = s_reads;
    out->errors = s_errors;
}
