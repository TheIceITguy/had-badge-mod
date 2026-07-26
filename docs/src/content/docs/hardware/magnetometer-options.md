---
title: Magnetometer options
description: Why the ICM-20948's built-in magnetometer is not usable on this badge, and the alternative parts being evaluated, with the identification traps for each.
---

The compass maths, calibration and smoothing in `util/compass.h` are sensor-agnostic and host-tested,
so the field can come from any part. That separation exists because the magnetometer inside the
ICM-20948 has not worked on this badge, and swapping it out must not mean rewriting the fusion.

Read [Compass and IMU](/had-badge-mod/hardware/compass/) first for the part that is fitted today, and
[Troubleshooting](/had-badge-mod/development/troubleshooting/#the-heading-is-suppressed-and-the-raw-field-reads-in-the-hundreds-of-ut)
for what has been eliminated by measurement.

## Why a second magnetometer is being evaluated at all

The AK09916 inside the ICM-20948 answers, resets, configures and streams correctly, and its self-test
shows a real response to the coil on its own die. It is not dead. What it delivers on this badge is a
stationary reading with 200 to 400 µT of spread on every axis, against an earth field of 25 to 65 µT.
The firmware suppresses the heading rather than publishing a bearing from that.

Three modules from two suppliers behaved identically, and the spread varies with sample rate, which is
what aliasing looks like: a disturbance faster than the 100 Hz sample rate folding into the samples.

That points at one specific weakness of the part. **The AK09916 has no oversampling control.** It
offers fixed output rates and nothing else, so there is no way to filter a fast disturbance before the
ADC samples it. Every alternative below can average internally, which is the one feature that
addresses this failure directly rather than working around it.

The accelerometer and gyroscope on the ICM's other die are fine, so the ICM stays fitted either way.
Tilt compensation needs the accelerometer, and roll and pitch have always been good.

## Addresses are clear for all of them

| Device | I2C address |
|--------|-------------|
| Keyboard TCA8418 | `0x34` |
| ICM-20948 | `0x68`, or `0x69` with AD0 high |
| QMC5883L | `0x0D` |
| BNO055 | `0x28`, or `0x29` with ADR high |
| LIS3MDL | `0x1C` or `0x1E` |
| BMM150 | `0x13` |

Nothing collides, so a second magnetometer needs no new pins. It parallels onto the same SDA and SCL
the compass already uses, plus power and ground.

## QMC5883L on a GY-271 board

### The silkscreen lies, and the chip marking is what identifies it

These boards are sold as HMC5883L. They are not. Genuine Honeywell HMC5883L is discontinued, so a
cheap board carrying that name is a QMC5883L from QST, which is a **different part with a different
register map and a different I2C address**.

Identify it by the chip marking, not the listing:

| Marking on the chip | Part | Address |
|---------------------|------|---------|
| `DA5883` | QMC5883L | `0x0D` |
| `L883` / `HMC5883L` | HMC5883L (Honeywell, discontinued) | `0x1E` |

The board evaluated here is marked `DA 5883` on the chip and `GY-271` / `HW-246` on the silkscreen, so
it is a QMC5883L. There is also a **QMC5883P** variant in circulation with yet another register map and
chip ID, so read the ID register before trusting anything.

Five pads: `VCC`, `GND`, `SCL`, `SDA`, `DRDY`. An onboard regulator accepts 3.3 V or 5 V on `VCC`.
`DRDY` is not needed; the driver polls the status register instead.

### Registers

| Register | Contents |
|----------|----------|
| `0x00`-`0x05` | X, Y, Z output, **little-endian**, 16-bit signed, two bytes each |
| `0x06` | Status: DRDY bit 0, OVL bit 1 (overflow), DOR bit 2 (data skipped) |
| `0x07`-`0x08` | Temperature |
| `0x09` | Control 1: MODE bits 1:0, ODR bits 3:2, RNG bits 5:4, OSR bits 7:6 |
| `0x0A` | Control 2: INT_ENB bit 0, ROL_PNT bit 6, SOFT_RST bit 7 |
| `0x0B` | SET/RESET period |
| `0x0D` | Chip ID, reads `0xFF` |

Two things about this part catch people out. **`0x0B` must be written `0x01`**; the datasheet gives no
alternative value and the part misbehaves if it is left alone. And a soft reset drops MODE back to
standby, so the reset has to come before the mode is set, never after.

### Configuration for this badge

Two choices differ from what a generic example would use, and both follow from what was measured on
the badge rather than from the datasheet defaults.

**Range: ±8 Gauss, not ±2 Gauss.** The ±2 G range is 200 µT full scale. The interference measured on
this badge is 200 to 400 µT, so the ±2 G range would clip on the noise alone and the samples would be
useless in a way that looks like a broken sensor. ±8 G gives 800 µT of headroom. The cost is
resolution: 3000 LSB/G at ±8 G against 12000 LSB/G at ±2 G, so 0.033 µT per count instead of
0.008 µT. Against a 50 µT field that is still about 1500 counts, which is ample.

**OSR: 512, the maximum.** OSR sets the bandwidth of an internal digital filter, so a larger value
means a narrower filter and less in-band noise. This is the setting the AK09916 does not have, and the
reason this part is worth trying at all. If the interference is genuinely above the sample rate, this
attenuates it inside the sensor where averaging in firmware cannot reach.

ODR can stay at 100 Hz for a 20 Hz consumer, or drop to 10 Hz if the spread justifies it.

### Using it

The driver is written and builds; it has not met the part yet. Four settings in the Compass group
control it, and the service reads them at boot, so a change needs a restart the way the pins do.

| Setting | Default | Choices |
|---------|---------|---------|
| `mag_source` | `imu` | `imu` (the AK09916 in the ICM-20948), `qmc5883l` |
| `qmc_range` | `8g` | `2g`, `8g` |
| `qmc_osr` | `512` | `512`, `256`, `128`, `64` |
| `qmc_odr` | `100` | `10`, `50`, `100`, `200` |

Wire it to the same SDA and SCL the compass already uses, plus 3V3 and GND. `DRDY` is not connected;
the driver polls the status register. Set `mag_source` to `qmc5883l` and restart.

Nothing falls back. If `mag_source` says `qmc5883l` and no module answers at `0x0D`, the badge boots
with no heading and says so, rather than quietly reverting to the AK09916. Reverting would present the
fault you are trying to escape as the new module's.

Two rows in Diagnostics change with this setting. **Field from** names the part and carries its own
counters, so the Data row above is not misread as describing a magnetometer when it is describing the
accelerometer's transport. Watch `ovl` there: it means the field went off the top of the selected
range, so the counts are clipped rather than merely noisy and `qmc_range` is too small. **Self-test**
reads `n/a for this part`, because only the AK09916 has a self-test coil and F3 is dropped for anything
else.

### The axis mapping is not done yet

The heading fuses this part's field with the ICM's accelerometer, so both have to be in the same frame,
and the transform depends on how the module is physically mounted. `qmc5883l_read()` currently applies
the identity mapping, which is almost certainly wrong for whatever orientation the module ends up in.

Determine it once, with the badge flat: read the Diagnostics field row while turning the badge through
north, east, south and west, note which sensor axis tracks which badge axis and with what sign, then
apply the swap and sign changes in `qmc5883l_read()`. Until that is done, expect the heading to be
rotated or mirrored even with a perfectly good sensor. Roll and pitch are unaffected, since they come
from the accelerometer alone.

## BNO055

### What it is

A Bosch smart sensor: a 14-bit accelerometer, a ±2000 °/s 16-bit gyroscope, a BMM150 magnetometer, and
a Cortex-M0 running Bosch's BSX3.0 fusion. It outputs absolute orientation directly, as Euler angles
or a quaternion at 100 Hz, alongside raw magnetic field at 20 Hz and temperature at 1 Hz.

The board being evaluated **has the 32.768 kHz crystal**, which matters: Bosch's fusion depends on it
and cheap boards routinely omit it.

### Why it may work where the AK09916 does not

Its magnetometer is a BMM150, which will see exactly the same interference. That is not the point. The
fusion uses the magnetometer only as a slow heading reference while the gyroscope carries short-term
motion, so a noisy field still yields a stable heading. The badge's current stack derives an
instantaneous heading from an instantaneous field reading, which is why 300 µT of noise lands straight
in the output with nothing to absorb it.

It also gives an independent verdict on the environment. The BNO055 reports per-sensor calibration
status, so if it cannot calibrate its magnetometer while sitting on the badge, that is confirmation
from a different vendor's part and a different algorithm that the badge itself is the problem.

### Two things to watch

**It stretches the I2C clock.** This is what makes the BNO055 awkward on a Raspberry Pi. ESP-IDF's I2C
master handles stretching, so it should be fine here, but it is the first thing to suspect if reads
fail or the bus wedges.

**Registers are paged.** Page 0 and page 1, selected by a page register, in the same way the ICM-20948
uses user banks. The same discipline applies: name the page before touching a register, and leave
page 0 selected.

### Using it

The driver is written and builds; it has not met the part yet. Set `mag_source` to `bno055` and
restart, plus `bno_addr_hi` if the board straps ADR high for `0x29`.

It is not a field source like the others. It reports a finished heading, so with it selected the
badge's tilt maths, hard-iron correction, calibration sweep and magnetometer self-test all sit idle and
only the low-pass is kept. The Compass app says so rather than offering an F1 sweep that could not do
anything, and the Calibration row reads `self, mag n/3`.

A heading is published once its magnetometer calibration reaches **2 of 3**. The part reports a
confident-looking bearing from the moment it powers up, long before its magnetometer has seen enough
for that to mean anything, so its own calibration status is the gate. Two rather than three, because
three needs a deliberate figure-of-eight and refusing a heading until then leaves the badge looking
broken when it is merely uncalibrated. Diagnostics shows all four figures.

The driver selects the external crystal and reads the bit back, so a board without one is reported
rather than silently running on the internal oscillator. It also brings the part up through a reset and
waits out the bootloader, because reading during that window returns zeroes that look like a working
part pointing north.

Unlike every other source, a BNO055 does not need the ICM at all: it has its own accelerometer, so a
badge with one fitted and no ICM still has a compass.

### The axis mapping is not done yet, and belongs in the chip

`AXIS_MAP_CFG_P1` and `AXIS_MAP_SIGN_P1` in `bno055.c` set the part's own frame, currently the
datasheet default. Fix it there rather than correcting the heading downstream: the fusion then runs in
the badge's frame and the reported roll and pitch need no correction either. Table 3-24 of the
datasheet lists the eight standard placements.

## Others considered

**LIS3MDL** (ST, `0x1C`/`0x1E`) is the best-documented of the group and has performance modes that are
internal averaging over 1, 3, 5 or 16 samples, with selectable ±4 to ±16 gauss. A good choice if the
QMC5883L proves as poorly specified as its datasheet suggests.

**MMC5603NJ** (MEMSIC) has the best noise performance and adds SET/RESET degaussing, which removes
offset drift rather than calibrating around it. Less reliably stocked.

**BMM150** (Bosch, `0x13`) has configurable XY and Z repetition counts, which is internal averaging by
another name. It is the same die that is inside the BNO055, so buying it separately only makes sense to
get raw access to it.
