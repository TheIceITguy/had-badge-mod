---
title: Conventions
description: The rules a change has to satisfy, covering layering, threading, style, settings, and tests.
---

These are the same rules as `CONVENTIONS.md` in the repository root, which is the normative
copy. Read that file before a first contribution.

## Portable logic comes first

Protocol, crypto, packet, region, NMEA, compass maths, settings, node DB, dedup, and UI layout
are plain C11 with no ESP-IDF, LVGL, or FreeRTOS includes. They live in `components/core`,
`components/mesh`, `components/util`, and the `portable/` subdirectories of `net` and `ui`. If
logic can be tested on a PC, it belongs there, because that is the only code the host suite can
reach.

Drivers own the hardware. `components/drivers` wraps the LCD, I2C, SPI, UART, PWM, ADC, and
power management, and everything else reaches hardware only through those drivers.

There is one source of truth for pins: `components/bsp/include/board_pins.h`.

## Only the `ui` task calls LVGL

Other tasks publish on the event bus or push to a queue, and the owning app drains it on its own
timer inside the UI task. Never touch an `lv_obj_*` from the radio, keyboard, or service tasks.

Event bus handlers run on the publisher's stack, so they stay short and do not block. Interrupt
handlers only give a semaphore or queue from IRAM; the decoding happens in a task.

## Match the surrounding file

C11, four-space indent, K&R braces. Default to file-local static and expose the minimum through
`include/<component>/`. Use fixed-capacity buffers and avoid malloc in the steady-state radio
and UI paths. Comments explain why, and cite the source they were ported from where that helps.

## Keep NVS keys short

NVS keys are limited to 15 characters, so keep keys short; labels can be long. Everything is
stored as a string, and typed access goes through `settings_get_*` and `settings_set_*`.

## Host tests gate every commit

Run the host tests before every commit. They are the only check that runs without hardware, and
they must be run from a shell where ESP-IDF has not been activated. See
[Host tests](/had-badge-mod/development/host-tests/).

On-device behavior is validated with the bring-up checklist.
