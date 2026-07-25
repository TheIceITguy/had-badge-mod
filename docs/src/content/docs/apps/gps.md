---
title: GPS
description: Read the current position from an attached GPS module, and wire it to the right pins.
---

The GPS app shows the current fix from an ATGM336H module. The module is optional, and the receiver is
brought up at boot, so a wiring or pin change needs a restart.

## Wiring lives on the hardware page

The module, the default pins (ESP RX on GPIO7, ESP TX on GPIO6), the J6 alternative and the mounting
notes are on [GPS module](/had-badge-mod/hardware/gps/). GPS is enabled by default, and the receiver is
brought up at boot, so a wiring or pin change needs a restart.

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
