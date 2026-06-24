---
title: Board and pins
description: The Communicator badge hardware and its GPIO map.
---

The target is the Hackaday 2024 Supercon Communicator badge.

| Part | Detail |
|------|--------|
| MCU | ESP32-S3-WROOM, 8 MB octal PSRAM, 16 MB flash |
| Display | NV3007 TFT, 428x142 used rotated, SPI at 80 MHz, RGB565 byte swapped |
| Radio | Semtech SX1262 LoRa, TCXO on DIO3 at 1.7 V, DIO2 plus GPIO10 RF switch |
| Keyboard | TCA8418 I2C matrix controller, interrupt driven |
| GPS (optional) | ATGM336H NMEA over UART on header J6 |
| Expansion | SAO v2 header (Simple Add-On): 3V3, GND, a second I2C bus and two GPIO |
| Power | LiPo with MCP73831 charger, backlight PWM on GPIO2 |

Every pin lives in one header, `components/bsp/include/board_pins.h`, so there is a single
place to change wiring.

## GPIO map

| Function | Pins |
|----------|------|
| Display SPI | MOSI 21, SCLK 38, DC 39, RST 40, CS 41, TE 42, backlight 2 |
| Radio SPI | NSS 17, MOSI 3, SCLK 8, MISO 9, RST 18, BUSY 15, DIO1 16, RF switch 10 |
| Keyboard I2C | SCL 14, SDA 47, INT 13, RST 48 |
| GPS UART (J6) | ESP RX 12 (from GPS TX), ESP TX 11 (to GPS RX) |
| SAO header (I2C + GPIO) | SDA 4, SCL 5, GPIO1 7, GPIO2 6 |

## Notes

The display and radio sit on separate SPI hosts (the display on SPI2, the radio on SPI3),
so they do not share a bus.

The GPS header J6 is a 4-pin expansion connector, not fitted from the factory. Its pin order is
3V3, IO11 (ESP TX), IO12 (ESP RX), GND. The IO12 pad is easily misread as "IO10"; GPIO10 is the
LoRa antenna switch and is not on J6. The badge also has an SAO port, J8, with I2C on GPIO4/GPIO5
and two spare GPIOs (7 and 6). A back-side PCB that breaks both headers out to JST connectors is
documented in [Internal addon board](/had-badge-mod/hardware/internal-addon-board/).

The stock badge has no magnetometer and no real-time clock. The compass features derive heading
from GPS course over ground (so the radar only orients while you move), and the clock is set
from the GPS time when a fix is available. An optional ICM-20948 can be added on the SAO header
to give a real tilt-compensated compass — see [SAO expansion and IMU](#sao-expansion-and-imu).

The battery sense pin is not confirmed on the stock board, so battery reporting is off by
default. Set the ADC pin and divider in the firmware once you know the schematic value.

## SAO expansion and IMU

Separate from J6, the badge carries a standard **SAO v2** connector (the Hackaday "Simple
Add-On", a 2x3 0.1" header). It brings out 3V3, GND, a second I2C bus and two spare GPIO:

| SAO signal | ESP32-S3 |
|------------|----------|
| SDA | GPIO4 |
| SCL | GPIO5 |
| GPIO1 | GPIO7 |
| GPIO2 | GPIO6 |
| 3V3 / GND | power |

This I2C bus is ESP32-S3 I2C unit 1, independent of the keyboard bus on unit 0, so an add-on
never contends with the keyboard. The pins live in `components/bsp/include/board_pins.h` as
`PIN_SAO_*`, `SAO_I2C_PORT`, and `IMU_I2C_ADDR`.

### ICM-20948 IMU / magnetometer

The recommended way to give the badge a real compass is a TDK **ICM-20948** (3-axis
accelerometer + gyroscope + magnetometer) on this bus. Its I2C address is `0x68` (`0x69` if AD0
is tied high) — no clash with the keyboard's TCA8418 at `0x34`. Wiring:

| ICM-20948 | SAO pad | ESP32-S3 |
|-----------|---------|----------|
| VDD / VDDIO | 3V3 | — |
| GND | GND | — |
| SDA | SDA | GPIO4 |
| SCL | SCL | GPIO5 |
| INT | GPIO2 | GPIO6 |
| (spare) | GPIO1 | GPIO7 |

To keep the sensor **inside the case**, desolder the 2x3 SAO header and wire the ICM-20948
breakout straight to the footprint pads. Match the SAO silkscreen labels rather than guessing
pin order.

There is no IMU driver in firmware yet. Once the sensor is mounted, the [Radar](/apps/radar/)
heading can come from the magnetometer (tilt-compensated with the accelerometer) instead of GPS
course, so the scope orients while you stand still.

## Case

The `hardware/case/` folder has a 3D-printable case that adds a bay for the GPS module so you
can carry the badge with the receiver attached. See [Case and enclosure](/had-badge-mod/hardware/case/)
for the file and printing notes.
