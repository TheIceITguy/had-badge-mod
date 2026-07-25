---
title: GPS
description: Read the current position from an attached GPS module, and wire it to the right pins.
---

The GPS app shows the current fix from an ATGM336H module. The module is optional, and the receiver is
brought up at boot, so a wiring or pin change needs a restart.

## Wiring

The default is the SAO header's two spare signal pins, because the compass takes the SAO I2C pair and
J6's IO12 drives the vibration motor:

| Signal | SAO pin | ESP32-S3 | Addon board |
|--------|---------|----------|-------------|
| 3V3 | J8 pin 1 | - | J2, J5 or J6 |
| GND | J8 pin 2 | - | J2, J5 or J6 |
| ESP RX from module TX | SAO_GPIO1, J8 pin 5 | GPIO7 | J3 pin 1 |
| ESP TX to module RX | SAO_GPIO2, J8 pin 6 | GPIO6 | J3 pin 2 |

So the module's TX goes to SAO_GPIO1 (GPIO7) and its RX to SAO_GPIO2 (GPIO6). Note the numbering
inversion: SAO_GPIO1 is ESP GPIO7 and SAO_GPIO2 is ESP GPIO6, which is an easy pair to swap by
mistake.

J8 is not populated from the factory, so solder to the pads or fit a header for the addon board.

### Using J6 instead

The original wiring still works, since both pins are settings. On J6 the pads are 3V3, IO11 (ESP TX),
IO12 (ESP RX), GND, so the module's TX goes to J6 pin 3 and its RX to J6 pin 2. Set GPS RX pin
(`gps_rx_pin`) to 12 and GPS TX pin (`gps_tx_pin`) to 11 and reboot. The IO12 silkscreen is easy to
misread as "IO10"; GPIO10 is the LoRa antenna switch and is not on that header. If the vibration motor
is enabled on its default GPIO12, turn it off or move it first, because the two cannot share the pad.

Because the RX and TX pins are settings, you do not have to match the default wiring. If the GPS page
shows `No data - check wiring`, the TX and RX are almost certainly swapped: either swap the two data
wires, or, without touching the wiring, swap the pin numbers in Settings (set GPS RX pin to the pad
your module's TX is on and GPS TX pin to the module's RX pad) and reboot.

## What it shows

The Status row tells you where the receiver is at:

| Status | Meaning |
|--------|---------|
| `Disabled (Settings)` | GPS is off in Settings, nothing is running |
| `No data - check wiring` | GPS is enabled but no NMEA is arriving over the UART. Check the wiring, power, and the RX/TX pins |
| `Searching...` | Sentences are coming in but there is no satellite lock yet |
| `Fix` | Locked, and the position rows are live |

The rows below it are satellites used and in view, fix quality (`GPS`, `DGPS` or `none`) with HDOP,
latitude, longitude, altitude in metres, speed in knots, course in degrees true, the GPS time in UTC,
and a Data row with how many NMEA sentences have been parsed and how long since the last byte. The
Data row is the fastest way to confirm the module is talking at all: `0 sent, none yet` means nothing
is arriving.

The firmware parses RMC, GGA, and GSV sentences. When the RMC sentence carries a valid time, the
system clock is set from it, which the badge uses for message timestamps and for naming track files.

## Course over ground is now a fallback heading

This page still reports the course the receiver gives, so the Course row is unchanged. What changed is
what the rest of the firmware does with it. With an [IMU compass](/had-badge-mod/apps/compass/) fitted,
enabled and calibrated, the bearing needles take their heading from the compass, as do Radar and Map
once F1 switches them from their default north-up to heading-up. GPS course over ground is then only
the fallback, used while you move faster than 1 knot. Without a compass it is still the only heading
the badge has.

The 1 knot threshold is there because course over ground is a direction of travel, not a heading:
standing still there is nothing for the receiver to derive it from, and it says nothing at all about
which way you are facing when you turn on the spot.
