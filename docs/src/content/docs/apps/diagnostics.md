---
title: Diagnostics
description: Live per-module status for the radio, GPS, compass, WiFi, Bluetooth, and system, plus one-press tests for the motor, the LED and the magnetometer.
---

Diagnostics shows live values that confirm each subsystem is working and the badge is configured the
way you expect. The rows are grouped under a heading per module, and two of them cannot be read at all,
only felt or seen: F1 buzzes the vibration motor once and F2 lights the notification LED for a second.
Both do nothing when the feature is off in Settings. F3 runs the magnetometer self-test, described
under Compass below.

## LoRa mesh

- Node: this badge's node id
- Radio: region, frequency, and spreading factor
- Channel: the channel name and the sync word
- RX/TX: decoded receive count and transmit count for this channel
- Heard: every valid LoRa frame the radio demodulates on any channel, counted before the channel and decrypt filter
- Signal: the last received signal strength and signal-to-noise ratio
- Peers: the number of known nodes

The RX/TX and Heard counters are the quickest way to tell what the radio is doing. If you send a
message and TX goes up, the firmware reached the radio. If a device is nearby but RX stays at zero,
look at Heard: if Heard is climbing the radio hears traffic and the problem is the channel or key, and
if Heard is also zero check the region and frequency against that device.

## GPS

- Fix: `off` when disabled in Settings, `no data` when the receiver is enabled but nothing is arriving over the UART, `searching` when sentences are coming in but there is no lock yet, or `fix` with the used and in-view satellite counts and HDOP once locked
- Pos: the current latitude and longitude
- Data: NMEA sentences parsed and how long since the last byte

The Data row is the quickest way to tell whether the module is talking at all. If it stays at
`0 sent, none yet`, the receiver is wired wrong, unpowered, or disabled.

## Compass

These rows cover the optional ICM-20948 on the SAO header. See
[Compass](/had-badge-mod/apps/compass/) for the app and
[Compass and IMU](/had-badge-mod/hardware/compass/) for the wiring.

| Row | What it reads |
|-----|---------------|
| State | `disabled`, `no IMU on SAO`, `no magnetometer`, `no data`, `uncalibrated`, `cal saved, unused`, `calibrating, N` with `(ready)` once the sweep is usable, or `ok` |
| Heading | The true heading, the magnetic heading before declination, and the declination itself, for example `276 true  mag 274  dec +2.4`. Only filled in when the State row says `ok` |
| Tilt | Roll and pitch in degrees from the accelerometer, for example `roll -3  pitch 12` |
| Data | The sensor identity, the driver's I2C counters, then the fused ones, for example `id EA  1840 rd, 0 e  1840 smp, 0s` |
| Self-test | The last magnetometer self-test verdict, blank until F3 is pressed |

The State row names the cause rather than the symptom, because the fixes have nothing in common:
`disabled` means `imu_enabled` is off, so the driver was never started. `no IMU on SAO` means the
setting is on but nothing answered on the SAO bus at boot. `no magnetometer` means the ICM-20948
answered but its AK09916 die did not, so the accelerometer works and no heading can ever be produced.
`no data` means the part is there but no fused heading is arriving. `uncalibrated` means samples are
flowing and nothing has been swept yet, while `cal saved, unused` means a calibration is stored and
`mag_cal_use` is holding it back, which another sweep would not fix.

The Heading row is how you check the declination: if every heading in the firmware is out by the same
amount, compare the magnetic value against a bearing you know. A magnetic reading that is right with a
true one that is not means the declination setting is wrong.

The Tilt row carries its own 2 second freshness rather than following the State row, so it reads out
even while the magnetometer is uncalibrated or silent. That is how you tell the two dies apart: tilt
moving with no heading means the accelerometer is fine and the magnetometer is the problem. Positive
pitch is nose up, the badge's top edge lifted; positive roll is left side up.

### Reading the Data row in parts

It walks the signal path from the bus to the published heading, so read it left to right:

- `id EA` is the raw WHO_AM_I byte the driver read from the ICM-20948 identity register. `EA` is the part answering correctly. `00` means nothing answered at all, because a NACK leaves the byte at zero, which separates an empty or miswired bus from something that answers but is not an ICM-20948. When the setting is on and bring-up failed, the row is just `id 00, no reads`, or the byte of whatever did answer
- `rd` and `e` are the driver's own successful reads and failed I2C transactions. They count transactions, not headings, and `e` includes magnetometer-only failures, which the fused counter cannot show: a magnetometer that is absent, quiet or saturated is not a transport failure, so the sample still arrives with the tilt in it and only the heading goes missing
- `smp` is fused headings, and the time after it is how long since the last one, or `none yet`

So a valid `id EA` with `rd` climbing, `e` climbing and `smp` stuck at 0 is a magnetometer die that is
not soldered down, while `e` climbing along with `rd` stuck is an I2C wiring problem on the main bus.
The row shows `--` when `imu_enabled` is off, since there is nothing to count.

### The Self-test row, and why it outranks the others

F3 energises a coil on the AK09916's own die and measures it, so the datasheet fixes the windows a
healthy part should land in: X and Y within +/-200 counts, Z between -1000 and -200. The reading is
the coil field plus the ambient one, so this does not isolate the sensor from its surroundings, but it
does isolate the analogue front end from the digital path that every other row exercises.

The row is blank until you press F3, because a verdict left over from a different module would be
worse than none. After that it reads `PASS` or `FAIL`, then how many of the five repeats landed
inside the windows, then the last set of counts:

```
Self-test  PASS  5/5  -38  91  -604
Self-test  FAIL  1/5  3975  3302  -11531
```

All five repeats have to pass. A part that passes one or two is not a working magnetometer having
bad luck, it is one returning numbers that occasionally land inside the window by chance, and the
counts printed alongside make the difference obvious.

Read the counts, not just the verdict. A few hundred out with Z biased negative is a real coil
response buried in noise, so the problem is interference. Thousands out with no coil response at all
points at the part, and calibration cannot help either way. Counterfeit modules and dead magnetometer
dies are common at the low-cost end of the ICM-20948 market, and the accelerometer and gyroscope on
the other die usually keep working, which is why the Tilt row above stays alive.

## WiFi

- State: the connection state, with signal strength when associated
- IP: the assigned address when the link is up

## Bluetooth

- State: `off`, `advertising`, or `connected`, for the Meshtastic companion link

## System

- Battery: charge percentage and voltage, or `disabled` when no battery sense is configured, which is the stock state because the board has no sense circuit
- Up/Heap: uptime and free heap
