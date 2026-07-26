/* See drivers/bno055.h. BNO055 in NDOF mode over the new I2C master driver. */
#include "drivers/bno055.h"
#include "drivers/i2c_bus.h"
#include "board_pins.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bno055";

/* Page 0 registers (Bosch BST-BNO055-DS000). The part has two register pages
 * selected by PAGE_ID, in the same way the ICM-20948 has user banks; everything
 * this driver touches is on page 0, and init leaves page 0 selected. */
#define R_CHIP_ID        0x00
#define R_PAGE_ID        0x07
#define R_MAG_DATA_X     0x0E   /* first of 6, little-endian pairs */
#define R_EUL_HEADING    0x1A   /* then roll at 0x1C, pitch at 0x1E */
#define R_TEMP           0x34
#define R_CALIB_STAT     0x35
#define R_SYS_STATUS     0x39
#define R_SYS_ERR        0x3A
#define R_UNIT_SEL       0x3B
#define R_OPR_MODE       0x3D
#define R_PWR_MODE       0x3E
#define R_SYS_TRIGGER    0x3F
#define R_AXIS_MAP_CFG   0x41
#define R_AXIS_MAP_SIGN  0x42

#define CHIP_ID_BNO055   0xA0

#define OPR_MODE_CONFIG  0x00
#define OPR_MODE_NDOF    0x0C   /* accel + gyro + mag, absolute orientation */
#define PWR_MODE_NORMAL  0x00
#define TRIGGER_EXT_XTAL 0x80   /* CLK_SEL: use the external 32.768 kHz crystal */
#define TRIGGER_RST_SYS  0x20

/* Units. Defaults are degrees and m/s^2, and the only reason to write UNIT_SEL is
 * to pin them rather than inherit whatever a previous session left. Bit 7 clear is
 * the Windows orientation convention, chosen because it is the datasheet default
 * and the axis mapping below has to be checked on hardware regardless. */
#define UNIT_SEL_DEFAULTS 0x00

/* Axis remap. The chip can rotate its own frame, which is better than doing it in
 * software: the fusion then runs in the badge's frame and the reported roll and
 * pitch need no correction either. 0x24/0x00 is the datasheet's default (P1).
 *
 * This is almost certainly wrong for however the module ends up mounted, and it
 * cannot be determined without the part in hand. Fix it here once, by reading the
 * Diagnostics rows while turning the badge, rather than correcting the heading
 * downstream. Table 3-24 in the datasheet lists the eight standard placements. */
#define AXIS_MAP_CFG_P1   0x24
#define AXIS_MAP_SIGN_P1  0x00

/* Scaling (datasheet section 3.6.5.x). */
#define EUL_LSB_PER_DEG   16.0
#define MAG_LSB_PER_UT    16.0

/* Timing. A reset takes the part through its bootloader, and a mode change is not
 * instant either: these are the datasheet's figures with room to spare, because
 * reading during the transition returns zeroes that look like a working part
 * pointing north. */
#define RESET_MS          700   /* datasheet: ~650 ms from POR to config mode */
#define MODE_SWITCH_MS     30   /* datasheet: 7 ms to an operating mode, 19 ms back */

static i2c_master_dev_handle_t s_dev;
static bool s_present, s_ext_crystal;
static uint8_t s_chip_id, s_sys_status, s_sys_err;
static bno055_calib_t s_calib;
static uint32_t s_reads, s_errors;

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

static void unpack_calib(uint8_t v, bno055_calib_t *c)
{
    c->sys   = (uint8_t)((v >> 6) & 0x03);
    c->gyro  = (uint8_t)((v >> 4) & 0x03);
    c->accel = (uint8_t)((v >> 2) & 0x03);
    c->mag   = (uint8_t)(v & 0x03);
}

esp_err_t bno055_init(int sda_pin, int scl_pin, bool addr_hi)
{
    s_present = false;

    i2c_master_bus_handle_t bus;
    esp_err_t e = i2c_bus_get(SAO_I2C_PORT, sda_pin, scl_pin, &bus);
    if (e != ESP_OK) return e;

    const uint16_t addr = addr_hi ? 0x29 : 0x28;
    if (!s_dev) {
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = SAO_I2C_HZ,
        };
        e = i2c_master_bus_add_device(bus, &cfg, &s_dev);
        if (e != ESP_OK) { ESP_LOGE(TAG, "i2c dev: %s", esp_err_to_name(e)); return e; }
    }

    /* The chip id is a real value here, unlike the QMC5883L's 0xFF, so it is a
     * usable presence test on its own. Retried because the part may still be in its
     * bootloader if the badge and the module powered up together. */
    for (int try = 0; try < 10; try++) {
        s_chip_id = 0;
        if (reg_read(R_CHIP_ID, &s_chip_id, 1) == ESP_OK && s_chip_id == CHIP_ID_BNO055) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (s_chip_id != CHIP_ID_BNO055) {
        ESP_LOGW(TAG, "no BNO055 at 0x%02X on sda=%d scl=%d (chip id 0x%02X, expected 0x%02X)",
                 addr, sda_pin, scl_pin, s_chip_id, CHIP_ID_BNO055);
        return ESP_ERR_NOT_FOUND;
    }

    /* Config mode for every register below: OPR_MODE, UNIT_SEL and the axis map are
     * all write-protected while the fusion is running, and writes to them are
     * silently dropped rather than refused. */
    reg_write(R_PAGE_ID, 0x00);
    reg_write(R_OPR_MODE, OPR_MODE_CONFIG);
    vTaskDelay(pdMS_TO_TICKS(MODE_SWITCH_MS));

    reg_write(R_SYS_TRIGGER, TRIGGER_RST_SYS);
    vTaskDelay(pdMS_TO_TICKS(RESET_MS));

    /* The reset drops the I2C peripheral too, so confirm it is back before
     * configuring; otherwise the writes below go nowhere and the failure surfaces
     * later as a part that reports zeroes. */
    s_chip_id = 0;
    for (int try = 0; try < 10 && s_chip_id != CHIP_ID_BNO055; try++) {
        reg_read(R_CHIP_ID, &s_chip_id, 1);
        if (s_chip_id != CHIP_ID_BNO055) vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (s_chip_id != CHIP_ID_BNO055) {
        ESP_LOGW(TAG, "BNO055 did not come back after its reset");
        return ESP_ERR_NOT_FOUND;
    }

    reg_write(R_PAGE_ID, 0x00);
    reg_write(R_PWR_MODE, PWR_MODE_NORMAL);
    reg_write(R_UNIT_SEL, UNIT_SEL_DEFAULTS);
    reg_write(R_AXIS_MAP_CFG, AXIS_MAP_CFG_P1);
    reg_write(R_AXIS_MAP_SIGN, AXIS_MAP_SIGN_P1);

    /* The external crystal, which this board has. Bosch's fusion accuracy depends
     * on it, and the bit only takes effect from config mode. Read back, because a
     * board without the crystal fitted would leave the part running off its
     * internal oscillator with no error reported anywhere. */
    reg_write(R_SYS_TRIGGER, TRIGGER_EXT_XTAL);
    vTaskDelay(pdMS_TO_TICKS(MODE_SWITCH_MS));
    uint8_t trig = 0;
    reg_read(R_SYS_TRIGGER, &trig, 1);
    s_ext_crystal = (trig & TRIGGER_EXT_XTAL) != 0;
    if (!s_ext_crystal)
        ESP_LOGW(TAG, "the external crystal was not selected: the board may not have one, "
                      "and the fusion is less accurate on the internal oscillator");

    reg_write(R_OPR_MODE, OPR_MODE_NDOF);
    vTaskDelay(pdMS_TO_TICKS(MODE_SWITCH_MS));

    uint8_t mode = 0xFF;
    reg_read(R_OPR_MODE, &mode, 1);
    if ((mode & 0x0F) != OPR_MODE_NDOF) {
        ESP_LOGW(TAG, "OPR_MODE wrote 0x%02X and reads 0x%02X: fusion is not running",
                 OPR_MODE_NDOF, mode);
        return ESP_FAIL;
    }

    reg_read(R_SYS_STATUS, &s_sys_status, 1);
    reg_read(R_SYS_ERR, &s_sys_err, 1);
    s_present = true;
    ESP_LOGI(TAG, "BNO055 up at 0x%02X in NDOF, %s crystal, sys_status %u, sys_err %u",
             addr, s_ext_crystal ? "external" : "internal", s_sys_status, s_sys_err);
    if (s_sys_err)
        ESP_LOGW(TAG, "BNO055 reports sys_err %u about itself", s_sys_err);
    return ESP_OK;
}

bool bno055_read(bno055_sample_t *out)
{
    if (!s_present) return false;

    /* Heading, roll and pitch in one transfer so the three belong to the same fused
     * sample. The field and the calibration status are separate reads because they
     * are diagnostics, not part of the orientation. */
    uint8_t eul[6];
    if (reg_read(R_EUL_HEADING, eul, sizeof eul) != ESP_OK) { s_errors++; return false; }

    uint8_t mag[6];
    if (reg_read(R_MAG_DATA_X, mag, sizeof mag) != ESP_OK) { s_errors++; return false; }

    uint8_t cal = 0;
    if (reg_read(R_CALIB_STAT, &cal, 1) != ESP_OK) { s_errors++; return false; }
    unpack_calib(cal, &s_calib);

    /* The fusion runs in whatever frame the axis map selected, so no rotation is
     * applied here. If the heading turns the wrong way or reads 90 degrees out, the
     * fix belongs in AXIS_MAP_CFG above, not in this arithmetic. */
    out->heading_deg = le16(&eul[0]) / EUL_LSB_PER_DEG;
    out->roll_deg    = le16(&eul[2]) / EUL_LSB_PER_DEG;
    out->pitch_deg   = le16(&eul[4]) / EUL_LSB_PER_DEG;

    out->mx = le16(&mag[0]) / MAG_LSB_PER_UT;
    out->my = le16(&mag[2]) / MAG_LSB_PER_UT;
    out->mz = le16(&mag[4]) / MAG_LSB_PER_UT;

    out->calib = s_calib;
    s_reads++;
    return true;
}

void bno055_get_status(bno055_status_t *out)
{
    out->present = s_present;
    out->chip_id = s_chip_id;
    out->ext_crystal = s_ext_crystal;
    out->calib = s_calib;
    out->reads = s_reads;
    out->errors = s_errors;
    /* Re-read rather than cached: sys_err is how the part reports a fault that
     * developed after boot, and a stale zero would hide it. */
    out->sys_status = s_sys_status;
    out->sys_err = s_sys_err;
    if (s_present) {
        reg_read(R_SYS_STATUS, &out->sys_status, 1);
        reg_read(R_SYS_ERR, &out->sys_err, 1);
        s_sys_status = out->sys_status;
        s_sys_err = out->sys_err;
    }
}
