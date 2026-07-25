---
title: GPS module
description: Wiring an ATGM336H receiver to the SAO spare GPIO pair, the J6 alternative, and what the badge does with a fix.
---

The badge has no receiver of its own. Wiring an **ATGM336H** to two spare pins gives it position,
course over ground, breadcrumb tracks, and a clock it sets itself. GPS is enabled by default in this
firmware, so a wired module works after a restart with nothing else to configure.

This page covers the module and the wiring. The [GPS app](/had-badge-mod/apps/gps/) page covers the
status rows and what each state means.

## Default wiring: the SAO spare GPIO pair

| Signal | SAO pad | J8 pin | ESP32-S3 | Addon board |
|--------|---------|--------|----------|-------------|
| 3V3 | 3V3 | 1 | - | J2, J5 or J6 |
| GND | GND | 2 | - | J2, J5 or J6 |
| ESP RX, from module TX | SAO_GPIO1 | 5 | GPIO7 | J3 pin 1 |
| ESP TX, to module RX | SAO_GPIO2 | 6 | GPIO6 | J3 pin 2 |

The module's TX goes to SAO_GPIO1 and its RX to SAO_GPIO2. Note the numbering inversion: SAO_GPIO1 is
ESP GPIO7 and SAO_GPIO2 is ESP GPIO6, which is an easy pair to swap by mistake.

The UART runs at 9600 baud, the ATGM336H default. Only the two data wires and power are needed; PPS
and any backup-battery pin can be left open.

Both expansion headers carry 3.3 V only. There is no 5 V on either, so a module with a 5 V-only
regulator will not work from the badge.

## Using J6 instead

J6 was the original home and still works, because both pins are settings. Its pads are 3V3, IO11
(ESP TX), IO12 (ESP RX), GND, so the module's TX goes to J6 pin 3 and its RX to pin 2. Set
`gps_rx_pin` to 12 and `gps_tx_pin` to 11, then restart.

Two things to know before you do. The vibration motor defaults to that same IO12 pad, so move or
disable it first. And the J6 silkscreen "IO12" reads as "IO10" at a glance; GPIO10 is the LoRa antenna
switch and is not on this header at all.

## Why the default moved off J6

The compass took the SAO I2C pair and the motor took J6's IO12, which left the two SAO spare GPIO as
the only free pair for a UART. Between them the three optional peripherals now use every signal pin on
both headers, so something had to move. Everything is a setting, so nothing here is binding.

## Settings

| Setting | Key | Type | Default |
|---------|-----|------|---------|
| GPS enabled | `gps_enabled` | bool | on |
| GPS RX pin (ESP) | `gps_rx_pin` | int, 0 to 48 | 7 |
| GPS TX pin (ESP) | `gps_tx_pin` | int, 0 to 48 | 6 |

All three are read once at boot, so a wiring or pin change needs a restart.

If the GPS page shows `No data - check wiring`, TX and RX are almost certainly swapped. Either swap the
two data wires, or leave the wiring alone and swap the pin numbers in Settings: set `gps_rx_pin` to the
pad your module's TX is on, and `gps_tx_pin` to the pad its RX is on.

## Mounting and the printed case

J8 is not populated from the factory, so solder to the pads, or fit a header and use the
[internal addon board](/had-badge-mod/hardware/internal-addon-board/), whose GPIO socket J3 carries the
pair. The [printed case](/had-badge-mod/hardware/case/) has a bay sized for the common ATGM336H
breakout, so the badge can be carried with the receiver attached rather than dangling.

Give the antenna a clear view of the sky and keep the module away from the LoRa antenna feed. A cold
start under a roof can take minutes; the GPS page's Data row is the fastest way to tell whether the
module is talking at all, before you start worrying about lock.

## What the badge does with a fix

- Position for [Radar](/had-badge-mod/apps/radar/), [Map](/had-badge-mod/apps/map/),
  [Tracker](/had-badge-mod/apps/tracker/) and the bearing needles
- Breadcrumb tracks written to SPIFFS by [Breadcrumbs](/had-badge-mod/apps/breadcrumbs/)
- The system clock, set from the RMC sentence, which is what timestamps messages and names track files
- Course over ground, which is the fallback heading when no [compass](/had-badge-mod/hardware/compass/)
  is fitted, usable only while you move faster than 1 knot

The firmware parses RMC, GGA and GSV. Position sharing over the mesh is off until you turn on
`mesh_share_pos`.
