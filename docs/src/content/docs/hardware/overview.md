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

## SAO expansion and IMU

Separate from J6, the badge carries a standard **SAO v2** connector at J8 (the Hackaday "Simple
Add-On", a 2x3 0.1" header). It brings out 3V3, GND, a second I2C bus and two spare GPIO:

| SAO signal | ESP32-S3 |
|------------|----------|
| SDA | GPIO4 |
| SCL | GPIO5 |
| GPIO1 | GPIO7 |
| GPIO2 | GPIO6 |
| 3V3 / GND | power |

This I2C bus is ESP32-S3 I2C unit 1, independent of the keyboard bus on unit 0, so an add-on never
contends with the keyboard. The pins live in `components/bsp/include/board_pins.h` as `PIN_SAO_*`,
`SAO_I2C_PORT`, and `IMU_I2C_ADDR`.

The compass has been confirmed this far on hardware: an ICM-20948 on this bus is detected and
identified, `WHO_AM_I` reads `0xEA`, and the AK09916 magnetometer on the second die answers and enters
continuous mode. So the register map, the user-bank switching and the bypass path to the magnetometer
work against the real part. What is still unproven is the heading itself. It has not been compared
against a known reference, and the axis transform between the two dies and the calibration sweep are
untested in the field, so treat a heading that looks plausible as plausible rather than as verified.

### The ICM-20948 sits on the SAO I2C bus at 0x68 or 0x69

The part the firmware drives is a TDK **ICM-20948**: a 3-axis accelerometer and gyroscope, plus an
AK09916 magnetometer on a second die inside the same package. Its I2C address is `0x68`, or `0x69` if
AD0 is tied high, so there is no clash with the keyboard's TCA8418 at `0x34`. The magnetometer has
its own fixed address, `0x0C`, and the driver puts the ICM in bypass so it appears as a second device
on the same bus. The bus runs at 400 kHz. Wiring:

| ICM-20948 | SAO pad | ESP32-S3 |
|-----------|---------|----------|
| VDD / VDDIO | 3V3 | - |
| GND | GND | - |
| SDA | SDA | GPIO4 |
| SCL | SCL | GPIO5 |
| INT (not used, leave open) | GPIO2 | GPIO6, the default GPS TX pad |
| (spare) | GPIO1 | GPIO7 |

The firmware polls the sensor over I2C, so only the two power pins, SDA and SCL have to be connected.
INT and GPIO1 can be left open.

Breakout labels are the trap here, not the badge. The cheap ICM-20948 modules (the WCMCU-20948 among
them) label their pins for SPI, so the two you want read `SCLK` and `SDI`, not SCL and SDA. On those
boards `SCLK` is the clock and goes to SAO SCL, `SDI` is the data line and goes to SAO SDA, `NCS`
selects I2C when it is high (usually pulled up on the module), and `SDO` doubles as the AD0 address
strap: pulled high it moves the part to 0x69, which is what `imu_addr_hi` is for. A module wired by
those names to the pins the labels suggest answers at no address at all.

![An ICM-20948 breakout next to a GPS module in the printed case, its pin header labelled VCC, GND, SCLK, SDI, NCS and SDO.](../../../assets/addon-board-wired.jpg)

J8 is a bare 2x3 through-hole footprint and ships **unpopulated**, so nothing has to be desoldered.
To keep the sensor inside the case, either solder the ICM-20948 breakout straight to the J8 pads, or
fit a header there and use the
[internal addon board](/had-badge-mod/hardware/internal-addon-board/), whose I2C socket J4 carries
SDA and SCL, with power from socket J2, J5 or J6. Match the SAO silkscreen labels rather than guessing
pin order.

### The driver moves counts, the service computes the heading

The driver in `components/drivers/imu_icm20948.c` is transport only: it identifies the part by its
WHO_AM_I byte, converts counts to g, degrees per second and uT at the finest full scales (2 g,
250 deg/s), and rotates the AK09916 axes into the accelerometer frame so the two dies can be fused.
The compass service in `components/services/compass_svc.c` owns the sampling task: it reads at 20 Hz,
applies the saved magnetometer correction, computes a tilt-compensated heading, adds the declination,
and publishes one smoothed true heading that the map-style apps read.

Nothing starts unless `imu_enabled` is on, and the setting is read once at boot, so enabling it or
moving its pins needs a restart. With it off, or with no part answering at the configured address,
the service logs one line naming the pins it tried and the badge boots normally, because a wrong pin
pair looks exactly like a dead part.

### No heading is published until the magnetometer is calibrated

Calibration is a sweep in the [Compass](/had-badge-mod/apps/compass/) app: turn the badge through
every orientation until at least 200 samples have arrived, every axis has seen a span of 20 uT, and
the measured field direction has been seen in all eight octants, then save. That last condition is
what refuses a sweep that only rocks the badge: rocking pushes all three spans past the floor while
leaving whole directions unseen, and the centre of a half-swept box is not the hard-iron offset. The
hard-iron offset and per-axis soft-iron scale go into their own NVS blob (namespace `compass`, key
`magcal`) and are applied to the next sample, so there is no reboot in the loop.

### Compass settings

| Setting | Key | Type | Default |
|---------|-----|------|---------|
| IMU compass enabled | `imu_enabled` | bool | on |
| Compass SDA pin | `imu_sda_pin` | int, 0 to 48 | 4 |
| Compass SCL pin | `imu_scl_pin` | int, 0 to 48 | 5 |
| I2C address 0x69 (AD0 high) | `imu_addr_hi` | bool | off |
| Declination (0.1 deg, E+) | `mag_decl_ddeg` | int, tenths of a degree, -1800 to 1800 | 0 |
| Use saved calibration | `mag_cal_use` | bool | on |

The bus pins are settings, so the sensor does not have to be on the SAO pair: wire it wherever two
pins are free and set them. `imu_addr_hi` covers a board that straps AD0 high, which moves the part
from 0x68 to 0x69 and otherwise looks exactly like a sensor that is not there.

Declination and `mag_cal_use` are re-read about once a second, so changing them takes effect without
a reboot. `imu_enabled` does not: turning the compass on or off needs a restart.

### Declination is a user setting because the badge carries no magnetic model

The firmware cannot derive the local angle between magnetic and true north from your position, so you
enter it. Look up the value for where you are, east positive, and give it in tenths of a degree, so
2.4 deg east is `24` and 8.0 deg west is `-80`.

Every bearing the firmware shows is true north, which is what makes the setting matter: get it wrong
and every heading is out by the same fixed number of degrees, while distances and bearings between
two positions stay correct. The Diagnostics Heading row prints the true heading, the magnetic heading
and the declination together for exactly that check.

### The bearing needles use the compass, Radar and Map only in heading-up

Once the compass is calibrated and fresh, [Tracker](/had-badge-mod/apps/tracker/), Follow and the
[Nodes](/had-badge-mod/apps/nodes/) row needles take their heading from it with no toggle to set.
[Radar](/had-badge-mod/apps/radar/) and [Map](/had-badge-mod/apps/map/) default to north-up and only
consult it once F1 selects heading-up. GPS course over ground remains the fallback while you move
faster than 1 knot, and those views stay north-up when neither source is usable, naming which of the
four reasons applies: no compass, nothing calibrated, a calibration switched off, or no sample yet.

## Vibration motor

An optional vibration motor buzzes on an incoming message, which makes the badge useful in a pocket
with the screen off. It is one signal: the firmware drives the pin high to vibrate and low to stop,
timed by a one-shot timer so nothing in the firmware blocks while it runs. The default is GPIO12,
J6's IO12 pad, and the pin is a setting.

| Setting | Default | What it does |
|---------|---------|--------------|
| `vibe_enabled` | on | Claims the pin at boot. Off leaves the pin free |
| `vibe_pin` | 12 | The GPIO driven high |
| `vibe_ms` | 180 | Buzz length in milliseconds, 20 to 2000 |
| `vibe_on_msg` | on | Buzz when a message arrives |

A burst of messages restarts the buzz rather than stacking pulses, so a busy channel is one buzz and
not a rattle. The pin is also driven low with its pull-down enabled at init, so the motor stays still
through the window between reset and the firmware starting.

### Drive the motor through a transistor

An ESP32-S3 pin sources tens of milliamps and a bare motor wants more than that, plus it kicks an
inductive spike back when it stops. Use a transistor or a motor driver with a flyback diode across
the motor. A module with a driver transistor already on it is the easy route.

If the log says the motor GPIO is also a GPS or compass pin, the two are fighting over one pad: move
one of them in Settings. The firmware warns rather than refusing, because only you know what is
soldered where.

## Case

The `hardware/case/` folder has a 3D-printable case that adds a bay for the GPS module so you can
carry the badge with the receiver attached. See
[Case and enclosure](/had-badge-mod/hardware/case/) for the file and printing notes.
