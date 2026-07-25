---
title: Vibration motor and notification LED
description: Two one-wire peripherals that make the badge useful with the screen off, and the transistor the motor needs.
---

Both of these exist for the same reason: a LoRa message can arrive while the badge is in a pocket with
the backlight off, and neither a dark screen nor a silent radio tells you about it. The motor buzzes,
and D1 blinks until you read it.

The LED is already on the badge. The motor is one wire you add.

## Vibration motor

The firmware drives a pin high to vibrate and low to stop, timed by a one-shot timer so nothing blocks
while it runs. The default is GPIO12, J6's IO12 pad, which is also the old GPS receive pin, so the two
are alternatives unless you move one.

| Setting | Key | Default | What it does |
|---------|-----|---------|--------------|
| Vibration motor | `vibe_enabled` | on | Claims the pin at boot. Off leaves the pin free |
| Motor GPIO | `vibe_pin` | 12 | The pin driven high |
| Buzz length (ms) | `vibe_ms` | 180 | 20 to 2000 |
| Buzz on message | `vibe_on_msg` | on | Buzz when a message arrives |

A burst of messages restarts the buzz rather than stacking pulses, so a busy channel is one buzz and
not a rattle. The pin is driven low with its pull-down enabled at init, so the motor stays still through
the window between reset and the firmware starting.

### Drive it through a transistor

An ESP32-S3 pin sources tens of milliamps, a bare motor wants more, and it kicks an inductive spike
back when it stops. Use a transistor or a small motor driver with a flyback diode across the motor. A
module with the driver transistor already on it is the easy route, and it is what the pin expects: one
logic-level signal, not a motor winding.

If the boot log says the motor GPIO is also a GPS or compass pin, the two are fighting over one pad.
Move one of them in Settings. The firmware warns rather than refusing, because only you know what is
soldered where.

## Notification LED

D1 is fitted on the stock badge, on GPIO1, and it sinks through the pin, so it is active low. The
firmware treats it the way a BlackBerry treated its one LED: since there is no colour to work with, the
vocabulary is timing.

| Setting | Key | Default | What it does |
|---------|-----|---------|--------------|
| Notification LED | `led_enabled` | on | Claims the pin at boot |
| LED GPIO | `led_pin` | 1 | D1 on the stock badge |
| LED is active low | `led_active_lo` | on | Off for an LED wired to source instead |
| Blink on unread message | `led_on_msg` | on | 60 ms every 3 seconds while a message is unread |
| Idle heartbeat (s, 0=off) | `led_beat_s` | 0 | A brief pulse on this interval, off by default |

The unread blink starts when a message arrives and stops when you read it, driven by the same two
events the rest of the firmware uses. The heartbeat is off by default because a pulse every few seconds
costs battery for no information.

Charging and low-battery patterns are the obvious next two, and the event bus already carries both
events, but this board has no battery sense circuit at all, so they would be unreachable code. See
[Board and pins](/had-badge-mod/hardware/overview/) for what a battery modification would take.

## Testing both without a second device

Open [Diagnostics](/had-badge-mod/apps/diagnostics/) and press F1 for the motor or F2 for the LED. F1
fires one pulse at the configured length even when `vibe_on_msg` is off, and F2 lights D1 for a second.

Both keys are no-ops when the feature is disabled, so a dead key means the setting is off rather than
the joint being bad. That distinction is the whole point: without these keys, checking a solder joint
meant getting somebody else to send you a message.

The boot log is the other half of the check. A working motor logs `vibe: vibration motor on GPIO12`,
and the LED logs `led: notification LED on GPIO1 (active low)`. If the pin could not be claimed, the
line says so instead.
