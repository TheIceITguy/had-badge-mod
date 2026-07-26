---
title: Troubleshooting
description: Build, host-test, boot, and peripheral problems seen so far, with the cause and the fix for each.
---

Every entry here is a fault seen on this firmware, with its cause and its fix. Match the symptom,
not the component you suspect.

## Build

### LVGL fails an integrity check

If the first build stops with a message about a missing `.component_hash` or `CHECKSUMS.json`
for `lvgl/lvgl`, the managed component download was interrupted and left incomplete. Delete
the partial state and let the component manager fetch it again:

```bash
rm -rf managed_components dependencies.lock build
idf.py set-target esp32s3
idf.py build
```

### lv_nv3007_create is undefined

LVGL's component does not expose the NV3007 driver through Kconfig, so a `CONFIG_LV_USE_NV3007`
line in `sdkconfig.defaults` is ignored. The top level `CMakeLists.txt` enables the driver for
the whole build with `-DCONFIG_LV_USE_NV3007=1`. If you removed that line, add it back.

### A header from another component is not found

If a build error says a component is not in the requirements list, add it to `REQUIRES` in
that component's `CMakeLists.txt`. The error message names both the component and the missing
dependency.

### A console encoding warning on Python 3.14

Set `PYTHONUTF8=1` in the shell before running `idf.py`.

## Host tests

### The host tests stop compiling right after a clean firmware build

You are running them in a shell where ESP-IDF has been activated. IDF's export script repoints
`python` at its own virtualenv, which has no `ziglang`, so `tools/run_host_tests.ps1` cannot
invoke `python -m ziglang cc` even though `pip install ziglang` succeeded in your normal Python.

Nothing in the error names ESP-IDF, which is what makes this confusing: the firmware built a
minute ago, so the test suite looks broken. Use a separate shell for the host tests, or install
`ziglang` into the IDF virtualenv as well.

## Boot

### The board reboots with a task watchdog on IDLE0

A task is busy-waiting and starving a core. The case seen during bring-up was the display
init: `lv_nv3007_create` runs the panel init and calls `lv_delay_ms`, which busy-waits on the
LVGL tick. The fix is to start the LVGL tick and register a yielding delay callback before
creating the display, which the firmware now does.

### Guru Meditation, interrupt watchdog, with a GPIO ISR in the backtrace

A GPIO interrupt is storming. The cause seen during bring-up was light-sleep wakeup:
`gpio_wakeup_enable` with a level trigger changes a pin's interrupt type to level triggered,
which collides with the edge ISR on the same pin. The radio DIO1 line then storms once it
goes high. The firmware keeps dynamic frequency scaling but does not enable level-triggered
GPIO wakeup, so light sleep is a later refinement.

### invalid panel io handle during display init

The NV3007 driver runs its init sequence inside `lv_nv3007_create`, before any display user
data is set. The send callbacks must use the panel IO handle from a file-local variable that
is set before the create call, not from LVGL user data.

### The screen is solid white, but the board boots and the radio works

The cause is usually stale `nvs` left by the badge's previous firmware, not hardware. The boot
log is healthy (`display: NV3007 display up` prints, the keyboard and radio come up, the node
announces) yet the panel stays a uniform lit white, and swapping the panel with a known-good
badge moves nothing, so the fault stays with the mainboard.

A plain `idf.py flash` never erases the `nvs` partition, so settings written by the old
firmware survive. They can be read back as out-of-spec values (the settings read path does not
clamp integers to their schema range, only the write path does) and break the running firmware
while the display init itself still reports success. A full chip erase clears it:

```bash
idf.py -p COM6 erase-flash
idf.py -p COM6 flash monitor
```

Do this once when first flashing over the original firmware (see
[Build and flash](/had-badge-mod/getting-started/building/)). If a confirmed-good panel is
still white after a full erase and reflash, suspect the hardware: reseat the display
FPC and continuity-check the control lines RST (GPIO40), CS (41), DC (39), SCLK (38), MOSI (21).

### The image is upside down or mirrored

Adjust the rotation in `components/drivers/display_nv3007.c`
(`lv_display_set_rotation`). If it comes out mirrored instead of rotated, change the panel
mirror flags. The column offset is set with `lv_nv3007_set_gap`.

## Compass and vibration motor

### no IMU on sda=4 scl=5 addr=0x68 in the boot log

Nothing acknowledged on the SAO I2C bus. The compass service logs the pins and the address
precisely because a wrong pair looks identical to a dead part, then leaves the sampling task
unstarted and reports OFF, so the badge boots normally without a compass.

Check, in this order: the SDA and SCL pads against `imu_sda_pin` and `imu_scl_pin` in Settings,
3V3 and GND at the sensor, and whether the board straps AD0 high (set `imu_addr_hi`, which moves
the part to `0x69`).

### The heading jumps constantly, even after a calibration sweep

Press F3 in Diagnostics before changing anything else. That runs the AK09916 self-test, which
energises a coil on the magnetometer's own die, so the datasheet gives the counts a healthy part
should return: X and Y within ±200, Z between −1000 and −200.

Read the result as a measurement of the coil field plus whatever ambient field is present. The test
does not isolate the sensor from its surroundings, so a strong or rapidly varying field nearby fails
it exactly as a broken die would. What it does isolate is the digital path: a part that answers,
resets and configures correctly but cannot return a repeatable coil measurement has either a dead
analogue front end or a very noisy environment, and both of those are hardware.

A PASS means the sensor and its surroundings are both fine, so an unstable heading is a calibration
problem. Sweep again, away from magnets and steel.

A FAIL prints how many of the five repeats passed and the last set of counts. Compare their size
against the windows rather than just reading the verdict. Counts a few hundred out with a visible
negative bias on Z are a working coil buried in noise, which is an interference problem. Counts in
the thousands, different every repeat, with no coil response at all point at the part. Counterfeit
and dead-magnetometer ICM-20948 modules are common at the low-cost end of the market, and the
accelerometer and gyroscope on the other die usually keep working, which is why the badge still
reports roll and pitch.

### The heading is suppressed and the raw field reads in the hundreds of uT

This is the state of the test badge, and it is not yet solved.
[Compass handoff](/had-badge-mod/development/compass-handoff/) is the full account, including what to
do when a replacement module arrives; the short version follows. The magnetometer answers over the
auxiliary master, resets, configures and streams, and its self-test shows a real coil response, but a
stationary part reads with 200 to 400 uT of spread on every axis when the earth's field is 25 to
65 uT. The firmware refuses to turn that into a bearing.

Eliminated by measurement: the access path (bypass replaced by the auxiliary master), the bus speed,
the aux master's poll rate, byte order, the register map, synchronising the read to the ICM's sample
boundary, backlight PWM interference, and the calibration and heading maths. Three modules from two
suppliers behave identically, so the part is unlikely to be the cause.

The spread changes with the sampling rate, which is what aliasing looks like: a disturbance faster
than the 100 Hz sample rate folded down into the samples. If you are debugging this, the useful next
steps are all hardware. Run the sensor on long wires well clear of the badge; power it from a
separate supply sharing only ground, SDA and SCL; and look at the 3V3 rail with a scope rather than a
multimeter, since switching ripple reads perfectly steady on a DMM.

The Compass app names this state as "Bad field" and Diagnostics counts the implausible samples,
because the raw field is checked against the 25 to 65 µT the earth actually produces. Without that
check `atan2` turns any pair of numbers into a confident-looking bearing, which is what makes a
dead magnetometer look like a firmware bug.

### Roll and pitch update but the heading never does

The accelerometer die is answering and the AK09916 magnetometer die is not. The compass service
publishes attitude on every good read and a heading only when the magnetometer read is valid,
which is what separates the two. Diagnostics shows the transport view: a valid WHO_AM_I with
rising I2C errors and no fused samples points at the magnetometer path, usually a solder joint.

### The heading stays unavailable and every view says "cal"

An uncorrected magnetometer can be tens of degrees out, so the heading is only offered as usable
once a calibration is in use. Run the sweep in the Compass app, or turn `mag_cal_use` back on if a
calibration is already stored. A sweep that only rocks the badge is refused; turn the badge
through every orientation.

### The GPS goes quiet after enabling the motor, or the motor does nothing

The two share a pin. `vibe_pin` defaults to `12`, which is J6's IO12 pad, and that is also the
GPS RX pad on a badge wired to J6. The service logs `GPIO12 is also a GPS UART pin; move one of
them` rather than refusing, because only you know what is soldered where. Move one of them:
`gps_rx_pin` and `gps_tx_pin` default to the SAO spare pins `7` and `6`, which leaves J6 free for
the motor.

## Radio

### No packets received

Open the Diagnostics app and check the region, preset, channel name, and sync word against
the device you expect to hear. The receive counter should climb when a packet is decoded. If
the radio never starts, check the TCXO voltage on DIO3 in the SX1262 driver.
