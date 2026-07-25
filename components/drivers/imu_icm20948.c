/* See drivers/imu.h. ICM-20948 over the new I2C master driver; AK09916 via bypass. */
#include "drivers/imu.h"
#include "board_pins.h"

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
#define B0_INT_PIN_CFG     0x0F
#define B0_ACCEL_XOUT_H    0x2D   /* first of 14: accel, gyro, temperature */
#define B2_GYRO_CONFIG_1   0x01
#define B2_ACCEL_CONFIG    0x14

#define WHOAMI_ICM20948    0xEA   /* section 8.1 */
#define MPU_WHO_AM_I       0x75   /* where the MPU-6050/9250 family keeps its ID */
#define PWR1_DEVICE_RESET  0x80   /* section 8.4, self-clearing */
#define PWR1_CLKSEL_AUTO   0x01   /* CLKSEL 1..5 = PLL when ready, else internal */
#define PWR2_ALL_ON        0x00   /* section 8.5, DISABLE_ACCEL/GYRO both 000 */
#define USER_CTRL_MST_OFF  0x00   /* clears I2C_MST_EN (bit 5), section 8.2 */
#define INT_PIN_BYPASS_EN  0x02   /* BYPASS_EN (bit 1), section 8.6 */
#define GYRO_CFG_250DPS    0x31   /* DLPFCFG 6 (5.7 Hz), FS_SEL 0, FCHOICE 1 */
#define ACCEL_CFG_2G       0x31   /* DLPFCFG 6 (5.7 Hz), FS_SEL 0, FCHOICE 1 */
#define AG_BURST           14

/* AK09916 magnetometer: a second die with its own I2C address, wired to the
 * ICM's auxiliary pins (DS-000189 sections 12 and 13, AK09916 datasheet). */
#define MAG_I2C_ADDR       0x0C   /* fixed in the package, no strap to read */
#define M_WIA1             0x00   /* company id, then device id at 0x01 */
#define M_ST1              0x10   /* first of 9: ST1, HXL..HZH, dummy, ST2 */
#define M_CNTL2            0x31
#define M_CNTL3            0x32
#define M_WIA1_AKM         0x48
#define M_WIA2_AK09916     0x09
#define M_CNTL2_POWERDOWN  0x00
#define M_CNTL2_CONT100HZ  0x08   /* continuous measurement mode 4 */
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

static i2c_master_dev_handle_t s_dev, s_mag;
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

/* Accel, gyro and temperature are big-endian; the magnetometer is little-endian. */
static int16_t be16(const uint8_t *p) { return (int16_t)((p[0] << 8) | p[1]); }
static int16_t le16(const uint8_t *p) { return (int16_t)((p[1] << 8) | p[0]); }

esp_err_t imu_init(int sda_pin, int scl_pin, int addr)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = SAO_I2C_PORT,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    esp_err_t e = i2c_new_master_bus(&bus_cfg, &bus);
    if (e != ESP_OK) { ESP_LOGE(TAG, "i2c bus: %s", esp_err_to_name(e)); return e; }

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

    /* Bypass (below) puts the magnetometer on this same bus, so it is a plain
     * second device. Reaching it through the ICM's aux master instead would mean
     * standing up a slave-proxy state machine and reading it out of a shadow
     * buffer, for a sensor this driver only ever polls. */
    dev_cfg.device_address = MAG_I2C_ADDR;
    e = i2c_master_bus_add_device(bus, &dev_cfg, &s_mag);
    if (e != ESP_OK) { ESP_LOGE(TAG, "i2c mag dev: %s", esp_err_to_name(e)); return e; }

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

    /* BYPASS_EN only connects the aux pins to this bus while the aux I2C master
     * is disabled (section 8.6), so clear I2C_MST_EN first. */
    reg_write(s_dev, B0_USER_CTRL, USER_CTRL_MST_OFF);
    reg_write(s_dev, B0_INT_PIN_CFG, INT_PIN_BYPASS_EN);

    /* Soft reset leaves the AK09916 in power-down with known registers, then
     * both id bytes are read in one go (they auto-increment 0x00 -> 0x01). */
    reg_write(s_mag, M_CNTL3, M_CNTL3_SRST);
    vTaskDelay(pdMS_TO_TICKS(1));
    uint8_t id[2] = { 0, 0 };
    if (reg_read(s_mag, M_WIA1, id, sizeof id) != ESP_OK ||
        id[0] != M_WIA1_AKM || id[1] != M_WIA2_AK09916) {
        /* Accel and gyro still give tilt, so keep going without a heading. */
        ESP_LOGW(TAG, "no AK09916 (id 0x%02X%02X): no heading", id[0], id[1]);
    } else {
        /* A mode change has to pass through power-down (AK09916 section 9.3),
         * and Twait is 100 us; one tick is the shortest wait available. 100 Hz
         * is the only continuous rate that always has a fresh sample ready for
         * a 20 Hz poller -- at 10 Hz half the reads would find DRDY clear. */
        reg_write(s_mag, M_CNTL2, M_CNTL2_POWERDOWN);
        vTaskDelay(pdMS_TO_TICKS(1));
        reg_write(s_mag, M_CNTL2, M_CNTL2_CONT100HZ);
        s_mag_present = true;
    }

    s_present = true;
    ESP_LOGI(TAG, "ICM-20948 up (id 0x%02X), magnetometer %s",
             s_whoami, s_mag_present ? "ready at 100 Hz" : "absent");
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

    /* ST1 through ST2 in one burst. ST2 is what tells the AK09916 the sample was
     * collected, so it has to be the last byte read or the next measurement
     * never lands (AK09916 section 13.4). */
    uint8_t m[MAG_BURST];
    if (reg_read(s_mag, M_ST1, m, sizeof m) != ESP_OK) { s_errors++; return true; }
    if (!(m[0] & M_ST1_DRDY) || (m[8] & M_ST2_HOFL)) return true;

    /* The magnetometer die is rotated 180 degrees about X relative to the
     * accel/gyro die: comparing figures 12 and 13 of DS-000189 section 15, mag
     * +X runs the same way as accel +X while mag +Y and +Z run the opposite way.
     * InvenSense's own driver uses the same diag(1, -1, -1) for the AK09916.
     * imu.h promises the accelerometer frame, so fix it here once, where the
     * datasheet is at hand, instead of in the fusion math. */
    out->mx =  le16(&m[1]) * MAG_UT_PER_LSB;
    out->my = -le16(&m[3]) * MAG_UT_PER_LSB;
    out->mz = -le16(&m[5]) * MAG_UT_PER_LSB;
    out->mag_ok = true;
    return true;
}

void imu_get_status(imu_status_t *out)
{
    out->present = s_present;
    out->mag_present = s_mag_present;
    out->whoami = s_whoami;
    out->reads = s_reads;
    out->errors = s_errors;
}
