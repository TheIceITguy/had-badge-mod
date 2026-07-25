---
title: Board and pins
description: Which GPIO each peripheral sits on, which of those pins are settings, and what moved in this release.
---

The target is the Hackaday 2024 Supercon Communicator badge. Three optional peripherals share its
two expansion headers, and between them they use every signal pin on both:

| Peripheral | Pins | Header | Addon board socket |
|------------|------|--------|--------------------|
| Compass (ICM-20948) | SDA 4, SCL 5 | J8, SAO I2C | J4 |
| GPS (ATGM336H) | ESP RX 7, ESP TX 6 | J8, SAO_GPIO1 and SAO_GPIO2 | J3 |
| Vibration motor | 12 | J6, IO12 pad | J1 pin 1 |

All three peripherals are enabled by default, and every pin in the table is a setting, so none of the
assignment is fixed. Wire a peripheral to any free pair and set the pins to match.

Two labels in that table are easy to get wrong. The SAO numbering runs against the ESP numbering:
SAO_GPIO1 is ESP GPIO7 and SAO_GPIO2 is ESP GPIO6. The J6 silkscreen "IO12" reads as "IO10" at a
glance; GPIO10 is the LoRa antenna switch and is not on J6 at all.

Neither header is fitted from the factory, so both are bare pads and you solder the header or the
wires yourself. Both carry the 3.3 V rail only: there is no 5 V on either, and the ESP32-S3 pins are
not 5 V tolerant, so every peripheral has to be a 3.3 V part. A back-side PCB that breaks both
headers out to JST connectors is documented in
[Internal addon board](/had-badge-mod/hardware/internal-addon-board/).

Every pin number is defined in one place, `components/bsp/include/board_pins.h`. If a page here
disagrees with that file, the file is right.

## The GPS default moved from J6 to the SAO spare GPIO pair

The GPS UART used to default to J6, ESP RX on IO12 and ESP TX on IO11. It now defaults to the SAO
header's two spare signal pins, ESP RX on GPIO7 and ESP TX on GPIO6, because the compass took the
SAO I2C pair and the motor took J6's IO12 pad.

J6 still works, since both ends are settings. Set `gps_rx_pin` to 12 and `gps_tx_pin` to 11, and
move or disable the motor first, because it defaults to that same IO12 pad. J6 is a 4-pin connector
whose pad order is 3V3, IO11 (ESP TX), IO12 (ESP RX), GND.

## Parts

| Part | Detail |
|------|--------|
| MCU | ESP32-S3-WROOM, 8 MB octal PSRAM, 16 MB flash |
| Display | NV3007 TFT, 428x142 used rotated, SPI at 40 MHz, RGB565 byte swapped |
| Radio | Semtech SX1262 LoRa, TCXO on DIO3 at 1.7 V, DIO2 plus GPIO10 RF switch |
| Keyboard | TCA8418 I2C matrix controller, interrupt driven |
| GPS (optional) | ATGM336H NMEA over UART on the SAO spare GPIO pair |
| Vibration motor (optional) | Driven high on a spare GPIO, J6's IO12 by default |
| IMU (optional) | TDK ICM-20948 accelerometer, gyroscope and AK09916 magnetometer on I2C, header J8 |
| Expansion | SAO v2 header (Simple Add-On): 3V3, GND, a second I2C bus and two GPIO |
| Power | LiPo with MCP73831 charger, backlight PWM on GPIO2 |

## GPIO map

| Function | Pins |
|----------|------|
| Display SPI | MOSI 21, SCLK 38, DC 39, RST 40, CS 41, TE 42, backlight 2 |
| Radio SPI | NSS 17, MOSI 3, SCLK 8, MISO 9, RST 18, BUSY 15, DIO1 16, RF switch 10 |
| Keyboard I2C | SCL 14, SDA 47, INT 13, RST 48 |
| GPS UART (SAO GPIO) | ESP RX 7 (from GPS TX), ESP TX 6 (to GPS RX) |
| SAO header (I2C + GPIO) | SDA 4, SCL 5, GPIO1 7, GPIO2 6 |
| Vibration motor | 12 (J6 IO12 pad), high vibrates |
| Debug LED | 1, active low |

The display and radio sit on separate SPI hosts, the display on SPI2 and the radio on SPI3, so they
do not share a bus.

This badge has no battery sense circuit: VBAT reaches the charger and the regulator and never an
MCU pin, so nothing can read the pack as it ships. Battery reporting is therefore off by default and
the sidebar hides the icon rather than showing an empty one. If you add a divider by hand, turn on
`bat_enabled` and set `bat_pin` and `bat_div_x100` in Settings; no firmware edit is needed. GPIO11
(the J6 IO11 pad) is the only ADC-capable pin left free, and it sits on ADC2, which the ESP32-S3
cannot read while WiFi is running.

The stock badge has no magnetometer and no real-time clock. Without an IMU the heading-up views fall
back to GPS course over ground, so they only orient while you move, and the clock is set from the GPS
time when a fix is available. Fitting an ICM-20948 on the SAO header gives a tilt-compensated compass
that holds while you stand still, and the heading-up views and bearing needles then take their
heading from it.

## The three optional peripherals have their own pages

Each one is a page rather than a section here, because fitting one is a soldering job with its own
pinout, its own settings and its own failure modes:

- [Compass and IMU](/had-badge-mod/hardware/compass/): an ICM-20948 on the SAO I2C pair, for a heading
  that holds while you stand still
- [GPS module](/had-badge-mod/hardware/gps/): an ATGM336H on the SAO spare GPIO pair, for position,
  course and a clock
- [Vibration motor and notification LED](/had-badge-mod/hardware/vibration-and-led/): one wire each,
  for knowing about a message with the screen off

The SAO header itself, J8, is a standard **SAO v2** connector (the Hackaday "Simple Add-On", a 2x3
0.1" header) carrying 3V3, GND, a second I2C bus and two spare GPIO:

| SAO signal | J8 pin | ESP32-S3 |
|------------|--------|----------|
| 3V3 | 1 | - |
| GND | 2 | - |
| SDA | 3 | GPIO4 |
| SCL | 4 | GPIO5 |
| GPIO1 | 5 | GPIO7 |
| GPIO2 | 6 | GPIO6 |

Note that GPIO4 and GPIO5 are pins **3 and 4**, not 4 and 5, and that the SAO numbering runs against
the ESP numbering. In the back view J8 pin 1 is the top of the rightmost column. This I2C bus is
ESP32-S3 unit 1, independent of the keyboard bus on unit 0, so an add-on never contends with the
keyboard.

## Case

The `hardware/case/` folder has a 3D-printable case that adds a bay for the GPS module so you can
carry the badge with the receiver attached. See
[Case and enclosure](/had-badge-mod/hardware/case/) for the file and printing notes.
