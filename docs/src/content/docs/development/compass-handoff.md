---
title: Compass handoff
description: The open compass fault, what has been ruled out by measurement, and the exact steps to take when the QMC5883L and BNO055 modules arrive.
---

The compass is the one unfinished part of this firmware. Everything needed to continue is written
down here so the next session does not repeat four days of elimination. Read this before touching
anything compass-related.

## State in one paragraph

The heading is suppressed, deliberately. The ICM-20948's AK09916 magnetometer answers, resets,
configures and streams correctly, and its self-test shows a real response to the coil on its own die,
so it is not a dead part. What it delivers on this badge is a stationary reading with 200 to 400 µT of
spread on every axis against an earth field of 25 to 65 µT, and the firmware refuses to turn that into
a bearing. Roll and pitch come from the accelerometer and have always been good. Two replacement
modules were ordered on 2026-07-26 and their drivers are written but have never met the hardware.

## Do not re-investigate these

Every item was eliminated by measurement on the badge, not by reasoning:

- **The access path.** Bypass mode was wrong and is fixed. The AK09916 sits on a private bus behind
  the ICM, and bypass reads its identity and control registers perfectly while returning noise for
  measurements, which is what made three modules look counterfeit. It now goes through the ICM's
  auxiliary master, SLV4 for configuration and SLV0 as a standing order into the shadow registers,
  matching every working driver for this package. Self-test counts dropped by a factor of five.
- **The aux master's poll rate.** `I2C_MST_ODR_CONFIG` at its reset value polls a 100 Hz magnetometer
  at 1.1 kHz, and every read touches ST2, the register that tells the die its data was consumed.
  Measured spread: 440 µT at 1.1 kHz against 200 µT at 69 Hz. Now set to 69 Hz.
- Bus speed, at 400 kHz and 100 kHz.
- Byte order, both ways.
- The register map, confirmed by a full single-byte scan.
- Synchronising the read to the ICM's own data-ready flag, which changed nothing.
- Backlight PWM interference, tested off and on with no difference.
- Accelerometer and gyroscope activity, tested with both powered down.
- The calibration and heading maths, which are host-tested.
- The parts themselves. Three modules from two suppliers behaved identically.

## The one clue that has not been chased

**The spread changes with the sample rate.** That is what aliasing looks like: a disturbance faster
than the 100 Hz sample rate folding down into the samples. Every remaining candidate is hardware, and
all three tests need someone holding the badge:

1. **Run the sensor on 30 cm of wire, well clear of the badge.** The decisive test, and free. If the
   spread collapses it is the badge's environment; if it does not, it is the module or the wiring.
   Drop the bus to 100 kHz first, since the wires get long.
2. **Power the module from a separate supply**, sharing only ground, SDA and SCL. A 3.25 V reading on
   a multimeter does not rule out supply noise, because switching ripple reads perfectly steady on a
   DMM.
3. **Scope the 3V3 rail at the module, AC coupled**, looking for ripple in the hundreds of kHz.

Test 1 decides which module to reach for first, so it is worth doing before either arrives. If the
badge is magnetically noisy, the BNO055 is the better bet, because it hides noise rather than
filtering it. If the badge is quiet, the QMC5883L should simply work.

## What is already built

`mag_source` in Settings selects the heading source, and the service reads it at boot, so changes need
a restart the way the pin settings do.

| `mag_source` | Part | What the badge does |
|--------------|------|---------------------|
| `imu` (default) | AK09916 in the ICM-20948 | Field from the ICM, fused with its accelerometer by `util/compass.h` |
| `qmc5883l` | Module at `0x0D` | Field from the module, fused with the ICM's accelerometer |
| `bno055` | Module at `0x28` | The part reports a finished heading; the badge's tilt maths, sweep and self-test all sit idle |

Nothing falls back. If the selected module does not answer, the badge boots with no heading and says
so, rather than quietly reverting to the AK09916 and presenting the fault you are escaping as the new
module's.

Both drivers build, both are unverified against hardware. See
[Magnetometer options](/had-badge-mod/hardware/magnetometer-options/) for the register-level detail and
the identification traps, which matter: the GY-271 boards are sold as HMC5883L and are not.

## When the modules arrive

Wire either one to the same SDA and SCL the compass already uses, plus 3V3 and GND. Neither needs a
new pin and no address collides. Then, in order:

### 1. Confirm it is the part you think it is

Read the chip ID before anything else. This project lost days to part identity. A GY-271 marked
`DA 5883` is a QMC5883L at `0x0D`; the driver refuses to run if the ID register disagrees, and a
`QMC5883P` has a different register map. A BNO055 reports `0xA0` at `0x00`.

### 2. Read the Diagnostics rows, not the heading

The **Field from** row names the part and carries its own counters. On a QMC5883L watch `ovl`: it
counts samples where the field exceeded the selected range, so the counts were clipped rather than
merely noisy and `qmc_range` is too small. On a BNO055 the four calibration figures are the point, and
`mag` below 2 is why a part that is plainly working still publishes nothing.

### 3. Fix the axis mapping before judging the heading

**Both drivers ship with an identity mapping that is almost certainly wrong.** The heading will be
rotated or mirrored even with a perfect sensor, and that is expected, not a bug to chase.

For the QMC5883L, its field is fused with the ICM's accelerometer, so the two have to share a frame.
Lay the badge flat, read the Diagnostics field row while turning it through north, east, south and
west, note which sensor axis tracks which badge axis and with what sign, then apply the swap and signs
in `qmc5883l_read()`.

For the BNO055, do it in the chip instead. `AXIS_MAP_CFG_P1` and `AXIS_MAP_SIGN_P1` in `bno055.c` set
the part's own frame, so the fusion runs in the badge's frame and the reported roll and pitch need no
correction either. Table 3-24 of the datasheet lists the eight standard placements.

### 4. Then judge it

Compare against a bearing you trust. Set `mag_decl_ddeg` for your location; the badge carries no
magnetic model, so declination is a user setting in tenths of a degree, east positive.

## Things worth doing that need no hardware

- **Fuse the gyroscope.** The ICM's gyroscope works and the compass stack ignores it. A complementary
  filter, gyro for short-term heading and magnetometer for long-term correction, is what the BNO055
  does internally. Being honest about the limit: pushing 300 µT of noise below a 48 µT signal needs
  roughly 350 samples averaged, so a magnetometer time constant of 20 to 30 seconds with the gyro
  carrying everything faster. ICM gyro bias drift over 30 seconds is plausibly a few degrees to tens
  of degrees, so this improves matters without rescuing them. It is the right architecture either way
  and makes the badge tolerant of a mediocre magnetometer.
- **A live spread readout in Diagnostics.** Rolling mean and standard deviation on screen, which turns
  each of the three hardware tests above into a glance instead of a rebuild.
- **A sphere-residual check on the calibration sweep.** The current gate accepts a sweep that only
  rocks the badge through one axis, because span and octant coverage together still cannot prove a
  full rotation.

## Where the code is

| Path | What |
|------|------|
| `components/util/compass.{c,h}` | All the maths. Portable, host-tested, sensor-agnostic. Do not put hardware here |
| `components/drivers/imu_icm20948.c` | ICM-20948 and the AK09916 via the aux master |
| `components/drivers/qmc5883l.c` | QMC5883L, unverified |
| `components/drivers/bno055.c` | BNO055 in NDOF mode, unverified |
| `components/drivers/i2c_bus.c` | Shared bus handles, so several drivers can hold the SAO pins |
| `components/services/compass_svc.c` | The 20 Hz task, source selection, NVS calibration |
| `components/apps/compass_app.c` | The Compass page and the sweep |
| `components/apps/diag.c` | The Compass section of Diagnostics |
| `host_tests/test_compass.c` | 1135 checks over the maths |
