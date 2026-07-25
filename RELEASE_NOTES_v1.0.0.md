# v1.0.0

v1.0.0 gives the badge a tilt-compensated compass, a vibration motor and a notification LED, and it
moves the GPS UART default off J6 to make room for them, so anyone upgrading a badge that already has
a GPS wired to J6 must read the breaking changes before flashing. The rest is additive: the offline Map
app, a map view in Breadcrumbs, the internal addon board, and a launcher fix that makes Radar and Map
reachable from the home screen for the first time. An ICM-20948 is detected and
identified on hardware, but no heading has been checked against a known bearing, so treat the compass
as beta. Details in [Verification status](#verification-status).

Previous release: v0.12.0.

## Breaking changes: check your GPS pins before you flash

The three changes below all affect a badge that was already working.

### The GPS UART default moved from J6 to the SAO spare pair

The compass takes the SAO I2C pair (GPIO4 and GPIO5) and the motor takes J6's IO12, so the GPS UART
moved to the SAO header's two spare signal pins.

| Setting | v0.12.0 default | v1.0.0 default | New pad |
|---------|-----------------|----------------|---------|
| `gps_rx_pin` (ESP RX, from module TX) | 12, J6 pin 3 | 7 | SAO_GPIO1, J8 pin 5 |
| `gps_tx_pin` (ESP TX, to module RX) | 11, J6 pin 2 | 6 | SAO_GPIO2, J8 pin 6 |

Note the numbering inversion on the SAO header: SAO_GPIO1 is ESP GPIO7 and SAO_GPIO2 is ESP GPIO6.
J6 still works, because both ends are settings: set `gps_rx_pin` to 12 and `gps_tx_pin` to 11.

### GPS, compass, vibration and the LED now default to enabled

| Setting | v0.12.0 default | v1.0.0 default |
|---------|-----------------|----------------|
| `gps_enabled` | off | on |
| `imu_enabled` | did not exist | on |
| `vibe_enabled` | did not exist | on |
| `led_enabled` | did not exist | on |

A badge with nothing fitted still boots normally. The compass service logs one warning naming the
pins and the address it tried and never starts its sampling task; the motor and LED drivers claim
their GPIOs and stay idle.

Each one claims its pins at boot when it is enabled, whether or not anything is attached: the GPS
opens its UART on GPIO7 and GPIO6, the compass opens the I2C bus on GPIO4 and GPIO5, the motor takes
GPIO12, and the notification LED takes GPIO1. If you want any of those six pins for something else,
turn its owner off.

### The vibration motor claims GPIO12, the old GPS RX pad

`vibe_pin` defaults to 12, which is J6's IO12, the pad the GPS RX used before this release. On a
stock badge the two are alternatives, not a pair. If you set the GPS pins back to J6, move
`vibe_pin` or turn `vibe_enabled` off first. The vibration service warns rather than refusing, with
`GPIO12 is also a GPS UART pin; move one of them` in the log, because only you know what is soldered
where.

### What you must do, by how your badge is set up

- GPS on J6 and you never edited the pins in Settings: the receiver goes quiet after the upgrade,
  because the stored value was absent and the new default applies. Set GPS RX pin to 12 and GPS TX
  pin to 11, move or disable the motor, and reboot
- GPS on J6 and you did edit the pins: nothing to do. NVS keeps them
- GPS on the SAO spare pair, or no GPS: nothing to do
- Motor to fit: wire it to GPIO12 through a transistor or a driver module with a flyback diode. An
  ESP32-S3 pin cannot drive a bare motor
- Compass to fit: see [SAO expansion and IMU](/had-badge-mod/hardware/overview/#sao-expansion-and-imu)
  for the wiring, then restart the badge. `imu_enabled` is read once at boot

NVS decides every one of those cases. `settings_get_*` reads the store first and falls back to the
compiled default only when the key has never been written, and nothing writes defaults at boot, so
a setting you changed before the upgrade survives it and a setting you never touched picks up the
new default. Anything you skip now stays available in Settings later, with one exception: an
`erase-flash` wipes NVS and puts every setting back to the v1.0.0 defaults.

## What is new

### The heading now holds while you stand still

A TDK ICM-20948 on the SAO header J8 gives a tilt-compensated magnetic heading, which is what GPS
course over ground can never do: course is a direction of travel, so it says nothing about which way
you are facing when you stop or turn on the spot. The part is optional and not fitted from the
factory. J8 is a bare 2x3 footprint and ships unpopulated, so the sensor solders straight to the
pads with nothing to desolder first.

The stack is four pieces:

- `components/drivers/imu_icm20948.c`, transport only: identifies the part by WHO_AM_I, sets the
  finest full scales (2 g accelerometer, 250 deg/s gyroscope), puts the ICM in bypass so the
  AK09916 magnetometer answers as a second device at 0x0C on the same 400 kHz bus, and rotates the
  magnetometer axes into the accelerometer frame
- `components/services/compass_svc.c`, the sampling task: reads at 20 Hz, applies the stored
  magnetometer correction, computes the heading, adds the declination, and publishes one snapshot
  smoothed by a circular low-pass with a 200 ms time constant
- `components/util/compass.c`, the portable maths: heading, roll and pitch, hard and soft-iron
  calibration, the circular filter, the heading-source arbitration and the shared wording every app
  uses for it
- `components/apps/compass_app.c`, the Compass app: the live state, heading, magnetic heading and
  declination, roll and pitch, calibration status, and the sweep itself on F1 and F2

Calibration is a sweep in the Compass app, and no heading is published until one is applied. Turn
the badge through every orientation until at least 200 samples have arrived, every axis has seen a
span of at least 20 uT, and the field direction has been seen in all eight octants, then press F1
to save. The hard-iron offset and per-axis soft-iron scale go to their own NVS blob (namespace
`compass`, key `magcal`) and apply to the next sample, so there is no reboot in the loop. A sweep
nobody comes back to is abandoned after 120 s.

Declination is a user setting, `mag_decl_ddeg`, in tenths of a degree with east positive, because
the badge carries no magnetic model and cannot derive the local value from its position. Every
bearing the firmware shows is degrees true. `mag_decl_ddeg` and `mag_cal_use` are re-read about once
a second; `imu_enabled` and the bus pins are read once at boot and need a restart.

Where the heading is used:

- Tracker, Follow and the Nodes row needles take it with no toggle to set, and the Nodes needles
  re-aim as you turn instead of waiting for the list to be rebuilt
- Radar and Map default to north-up and consult it only after F1 selects heading-up
- The Breadcrumbs map view is always north-up and never consults it
- GPS course over ground is the fallback in all of them, used above 1 knot, and the view stays
  north-up when neither source is usable

A view that wanted a heading and is showing north names which of four reasons applies (`move`,
`cal`, `cal off`, `wait`), because each one needs a different action and telling someone to walk
when the answer is a calibration sweep is the failure that wording exists to prevent. Diagnostics
gained four compass rows: state, heading with the magnetic heading and declination beside it, roll
and pitch with their own freshness, and the driver's transport counters. Roll and pitch come from
gravity alone and keep updating when only the magnetometer die is silent, which is how you tell the
two dies inside the package apart.

### An incoming message buzzes in your pocket

A vibration motor on a spare GPIO makes the badge useful with the screen off. Driving the pin high
vibrates; an esp_timer one-shot puts it back down, so nothing blocks waiting for a buzz to end. A
second message arriving mid-buzz restarts the pulse rather than stacking, so a busy channel is one
buzz and not a rattle. The pin is driven low with its pull-down enabled at init, so the motor stays
still through the window between reset and the firmware starting.

| Setting | Default | What it does |
|---------|---------|--------------|
| `vibe_enabled` | on | Claims the pin at boot |
| `vibe_pin` | 12 | The GPIO driven high |
| `vibe_ms` | 180 | Buzz length in milliseconds, 20 to 2000 |
| `vibe_on_msg` | on | Buzz when a message arrives |

### D1 blinks when a message is waiting

The stock debug LED on GPIO1 is now a notification LED. An unread message blinks it for 60 ms every
3000 ms, brief enough to leave running for hours and slow enough to notice across a room, and the
blink stops when the messages are read (`EV_MESSAGE_RECEIVED` sets the state, `EV_MESSAGES_READ`
clears it). An optional idle heartbeat blinks 30 ms on whatever period you set. Patterns are policy
and live in `components/services/led_svc.c`, walked by a 50 ms timer with the highest-priority state
winning; `components/drivers/led.c` only turns the pin on and off.

| Setting | Default | What it does |
|---------|---------|--------------|
| `led_enabled` | on | Claims the pin at boot |
| `led_pin` | 1 | The LED GPIO |
| `led_active_lo` | on | Matches the stock D1, which the GPIO sinks |
| `led_on_msg` | on | Blink while a message is unread |
| `led_beat_s` | 0 | Idle heartbeat period in seconds, 0 to 3600, 0 is off |

Charging and low-battery patterns are deliberately absent: the event bus carries both, but this
board has no working battery sense (see [Known limitations](#the-battery-indicator-has-never-worked)),
so they would be unreachable.

The motor and the LED are the two peripherals a status row cannot confirm, so Diagnostics fires them
on demand: F1 buzzes the motor once, F2 lights the LED for 1000 ms. Both are no-ops when the feature
is off in Settings.

### Diagnostics can fire the motor and the LED

The two peripherals whose wiring cannot be checked by reading a row now have a key each. In
Diagnostics, F1 buzzes the motor once at the configured length, and F2 lights D1 for a second. F1
fires even when `vibe_on_msg` is off, because the point is to test the wiring rather than the policy,
and both are no-ops when the feature is disabled, so a dead key means the setting is off rather than
the joint being bad.

Without them, checking a solder joint on the motor meant getting a second device to send a message.

### Every new peripheral pin is a setting

Nothing added in this release is soldered to a fixed pin. The compass bus, the motor GPIO, the LED
GPIO and the GPS UART are all settings, so a badge wired differently is configured rather than
recompiled.

| Setting | Type | Default |
|---------|------|---------|
| `imu_sda_pin` | int, 0 to 48 | 4 |
| `imu_scl_pin` | int, 0 to 48 | 5 |
| `imu_addr_hi` | bool | off, so 0x68; on selects 0x69 for a board that straps AD0 high |
| `vibe_pin` | int, 0 to 48 | 12 |
| `led_pin` | int, 0 to 48 | 1 |
| `gps_rx_pin` | int, 0 to 48 | 7 |
| `gps_tx_pin` | int, 0 to 48 | 6 |

Settings now sit in three new groups, Compass, Vibration and LED, and the registry cap went from 48
to 64 slots (the firmware registers 53 keys).

### Map is a full app, and Breadcrumbs can draw its trail

The offline vector basemap that used to exist only as a Radar underlay is now an app of its own.
Map draws roads and water from `/spiffs/map.vmap` full-screen with you at the centre, plus your
recorded trail and every positioned mesh node. F1 switches north-up and heading-up, F2 steps the
range through 100 m, 200 m, 500 m, 1 km and 2 km from the centre to the nearest edge, and F3
unlocks free panning with the arrow keys. Panning is always north-up, whatever F1 says.

Breadcrumbs gained a map view on F2: the recorded trail drawn over the same basemap at a fixed
500 m scale, always north-up, with no orientation toggle so the trail has the same shape every time
you open it. Radar's overlay and both new views now render through one shared widget,
`components/ui/map_canvas.c`, and the rectangular line clip they need lives in the host-tested
`components/util/map_proj.c`.

### The expansion headers have a breakout board and a documented pinout

`hardware/internal-addon-board/` holds a back-side PCB that solders onto J8 (SAO) and J6 and
re-exposes both as six 2-pin JST sockets at 2 mm pitch, one functional pair per socket, so the GPS,
the compass and the motor plug in instead of being soldered. The folder carries the Fritzing sketch,
the board outline (42.0 x 37.5 mm), a placement overlay over the badge back, the gerber export with
a ground pour, and two Fritzing badge parts extracted from the official KiCad design rather than
drawn by eye. The hardware docs now cover the SAO v2 header, the ICM-20948 wiring, the motor drive
requirement and which peripheral owns which pin by default.

## Fixes

### The launcher capped itself at 8 tiles, hiding Radar and Map

`launcher.c` held an 8-slot tile array and clamped its input to it, so any app past index 7 was
silently unreachable from the home screen. v0.12.0 registered 10 apps, which put Radar at index 9
outside the strip; Map arrived at index 10 on this branch and was hidden the same way. The cap is now
`TILE_MAX 16` against 12 registered apps, and the strip scrolls horizontally, so extra tiles cost
nothing but the array. Anyone running v0.12.0 has been unable to open Radar from the launcher.

### The Nodes list had its own copy of the cardinal-point helper

`nodes.c` carried a local `cardinal()` that has been replaced by the shared `compass_cardinal()`, so
one implementation now answers for every app.

## Verification status

The firmware builds clean for `esp32s3`, the host suite is green, and the build boots on real
hardware with the GPS, the motor and the notification LED live on their pins. An ICM-20948 answers on
the SAO bus and identifies itself, so the compass transport is confirmed; the heading it produces has
not been compared against a known bearing.

| Check | Result |
|-------|--------|
| Build, `idf.py set-target esp32s3 && idf.py build` | Clean, at the tree that produced the flashed binary |
| Host tests, `pwsh tools/run_host_tests.ps1` | 1113 checks, 0 failures |
| Compass maths coverage | 388 of those 1113 checks are the new compass suite |
| Boot on hardware | Boots clean, flashed from this tree |
| GPS on GPIO7 and GPIO6 | Confirmed live |
| Vibration motor on GPIO12 | Confirmed live |
| Compass against an ICM-20948 | Not confirmed. See below |
| Notification LED on GPIO1 | Not confirmed. The LED service landed after the build that was flashed |

### The sensor is confirmed, the heading is not

An ICM-20948 wired to the SAO bus is detected and identified on hardware: `WHO_AM_I` reads `0xEA`,
and the AK09916 magnetometer on the second die answers and enters continuous mode at 100 Hz. That
clears the parts of the driver that could not be tested without the part in hand, namely the register
map, the user-bank switching, the bypass path to the second die and the identification checks.

Three things above that layer still have not executed against the real part:

- The heading output. No measured heading has been compared against a known bearing
- The axis transform between the two dies. `imu_read()` negates the AK09916 Y and Z axes to bring
  the magnetometer into the accelerometer frame; the sign convention is from the datasheet, not from
  a bench check, and getting it wrong yields a heading that looks plausible and is wrong
- The calibration flow end to end. The sweep gates, the NVS blob and the correction are host-tested
  against synthesised samples, and the on-device path from a real sweep to a corrected heading has
  not been walked

The 388 host-test checks cover the maths, not the hardware: the heading against samples synthesised
from a known attitude, tilt invariance (25 degrees of roll must not move the needle, and the check
is proven to bite by failing the uncompensated version), the roll and pitch signs, calibration
recovered from a swept box, the refusal of a sweep that only rocks the badge, dropped magnetometer
reads being excluded from a sweep, the circular filter across the 0/360 seam, the source
arbitration, and the exact label text for each of the four north-up reasons. None of that
substitutes for one bench comparison against a known bearing. Treat the compass as beta in this
release, and read the heading in the Compass app against a bearing you trust before relying on it.

### The notification LED initialises, but its patterns have not been watched

A flashed badge logs `led: notification LED on GPIO1 (active low)`, so the driver claims the pin and
the service starts. What has not been observed is the light itself: the unread-message blink, the
idle heartbeat and the active-low polarity on D1 have been read in the code and not seen with eyes.
Diagnostics F2 lights it for a second, which is the fastest way to settle the polarity question.

## Known limitations

### A sweep can pass all three gates and still be wrong

The three conditions (200 samples, a 20 uT span on each axis, all eight octants) are necessary but
not sufficient. Spinning the badge through a full turn while holding it roughly level satisfies all
three, and the vertical component of the earth's field is never measured from both sides, which at
Swiss latitudes leaves the vertical axis offset out by tens of uT. That error cancels while the
badge is level and grows as you tilt it, so the symptom is a heading that looks right in the hand
and wanders as the badge tips. Sweep again with steeper tilts, nose up and nose down, on both sides,
face up and face down.

### The battery cannot be measured on this board at all

This is not a missing setting, it is a missing circuit. Every net label in the upstream schematic is
accounted for by the keyboard, the LCD, the radio, the SAO header, USB and the power rails, and there
is no sense net: `VBAT` reaches the MCP73831 charger and the AP2112K regulator and never an MCU pin.
R10 and R11, the two 100k parts that look like a divider, sit around the DMG2305 load switch.

What this release changes is the shape of the workaround. The sidebar now hides the battery icon
instead of showing a permanently empty one, because an empty glyph claims a flat pack while the truth
is that nothing is measuring. And the sense path is a Battery settings group rather than a firmware
edit: `bat_enabled`, `bat_pin` (default 11) and `bat_div_x100` (default 200, a 100k/100k pair). Add a
divider from VBAT to a free ADC pin and three settings bring it up.

One constraint if you do: every ADC1 pin is taken, so the only free candidate is GPIO11 on the J6
IO11 pad, and GPIO11 is on ADC2, which the ESP32-S3 cannot read while WiFi is active. Reads fail
during a web-UI session and the icon disappears again until WiFi stops.

### The SAO I2C bus stays up even when no sensor answers

`imu_init()` creates the bus before it discovers the part is missing, and does not delete it on that
path, so with `imu_enabled` on and nothing fitted GPIO4 and GPIO5 stay claimed by the I2C driver for
the rest of the session. Turning `imu_enabled` off is the only way to leave them free.

### The compass needs a restart, the declination does not

`imu_enabled`, `imu_sda_pin`, `imu_scl_pin` and `imu_addr_hi` are read once at boot. Changing any of
them needs a restart. `mag_decl_ddeg` and `mag_cal_use` are re-read about once a second and take
effect without one.

### The motor cannot hang off the GPIO

An ESP32-S3 pin sources tens of milliamps, a bare motor wants more, and it kicks an inductive spike
back when it stops. Drive it through a transistor or a driver module with a flyback diode across the
motor.

## How to upgrade

```
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

If the first boot is blank or white, that is almost always stale NVS left by the previous firmware.
Run `idf.py -p <PORT> erase-flash` once, then flash again. Note what that costs you here: an
erase-flash wipes the settings with everything else, so a badge you had configured comes back on the
v1.0.0 defaults, GPS on GPIO7 and GPIO6 with the motor on GPIO12. Redo the pin changes from
[Breaking changes](#breaking-changes-check-your-gps-pins-before-you-flash) afterwards. The saved
magnetometer calibration lives in NVS too and does not survive an erase.

Flashing is reversible: the original MicroPython image can go back over USB at any time.

## Assets

| Asset | Path |
|-------|------|
| Firmware binary, esp32s3 | `build/had-badge-mod.bin` |
| Addon board gerber archive | `hardware/internal-addon-board/internal-addon-board-gerbers.zip` |

The firmware version reported on the home screen is `1.0.0` plus the git commit.

The binary in `build/` was built from this tree, after the notification LED and the battery settings
landed, and is the image that was flashed and booted for the verification above.
