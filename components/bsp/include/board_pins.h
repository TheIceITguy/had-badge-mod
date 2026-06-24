/* Single source of truth for every GPIO on the Hackaday 2024 Communicator badge
 * (ESP32-S3). Ported from firmware/badge/hardware/board.py and lvgl_setup.py. */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

/* --- Display: NV3007 TFT, 142x428 native (used at 428x142 rotated 270) ---- */
#define PIN_LCD_MOSI       21
#define PIN_LCD_SCLK       38
#define PIN_LCD_DC         39
#define PIN_LCD_RST        40
#define PIN_LCD_CS         41
#define PIN_LCD_TE         42
#define PIN_LCD_BACKLIGHT  2        /* PWM, active LOW */
#define LCD_SPI_HOST       SPI2_HOST
#define LCD_SPI_HZ         (40 * 1000 * 1000)   /* SCLK/MOSI route via GPIO matrix (not SPI2 IOMUX), so 40 MHz is the reliable ceiling; 80 MHz is per-unit flaky */
#define LCD_NATIVE_W       142
#define LCD_NATIVE_H       428
#define LCD_OFFSET_X       0
#define LCD_OFFSET_Y       12
#define LCD_BL_DUTY_MAX    1023     /* 10-bit LEDC */

/* --- Radio: SX1262 LoRa (SPI host 3) -------------------------------------- */
#define PIN_RF_MOSI        3
#define PIN_RF_SCLK        8
#define PIN_RF_MISO        9
#define PIN_RF_NSS         17
#define PIN_RF_RST         18
#define PIN_RF_BUSY        15
#define PIN_RF_DIO1        16       /* IRQ */
#define PIN_RF_SW          10       /* RX/TX antenna switch (1 = ?) */
#define RF_SPI_HOST        SPI3_HOST
#define RF_SPI_HZ          (2 * 1000 * 1000)

/* --- Keyboard: TCA8418 I2C matrix controller ------------------------------ */
#define PIN_KBD_SCL        14
#define PIN_KBD_SDA        47
#define PIN_KBD_INT        13       /* active LOW interrupt */
#define PIN_KBD_RST        48
#define KBD_I2C_PORT       0
#define KBD_I2C_ADDR       0x34     /* TCA8418 fixed address */
#define KBD_I2C_HZ         400000

/* --- GPS (optional): ATGM336H NMEA over UART on header J6 ------------------ */
#define PIN_GPS_TX         11       /* ESP TX -> GPS RX (optional) */
#define PIN_GPS_RX         12       /* ESP RX <- GPS TX (NMEA in) */
#define GPS_UART_NUM       1
#define GPS_UART_BAUD      9600

/* --- SAO v2 expansion header (Simple Add-On, 2x3 0.1") -------------------- */
/* Standard SAO pinout brings out 3V3, GND, a second I2C bus and two spare GPIO.
 * The I2C lines are independent of the keyboard bus (KBD_I2C_PORT 0), so an
 * add-on never contends with the keyboard. Used for the optional ICM-20948
 * IMU/magnetometer; to mount that sensor inside the case the 2x3 header is
 * desoldered and the chip wired straight to the footprint pads. */
#define PIN_SAO_SDA        4        /* SAO_SDA  */
#define PIN_SAO_SCL        5        /* SAO_SCL  */
#define PIN_SAO_GPIO1      7        /* SAO_GPIO1 - spare / IMU FSYNC or 2nd INT */
#define PIN_SAO_GPIO2      6        /* SAO_GPIO2 - IMU INT (wake-on-motion)     */
#define SAO_I2C_PORT       1        /* ESP32-S3 I2C unit 1 (unit 0 = keyboard) */
#define SAO_I2C_HZ         400000   /* ICM-20948 fast-mode I2C ceiling         */
#define IMU_I2C_ADDR       0x68     /* ICM-20948 AD0=0 (0x69 if AD0 tied high) */

/* --- Misc ----------------------------------------------------------------- */
#define PIN_DEBUG_LED      1        /* active LOW */
#define PIN_BAT_ADC        (-1)     /* unconfirmed on this board; off by default */

#endif /* BOARD_PINS_H */
