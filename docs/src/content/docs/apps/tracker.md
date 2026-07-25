---
title: Tracker
description: Point a compass needle at a live Meshtastic node and read what that node transmits.
---

Tracker points a compass needle at another Meshtastic node and shows what that node transmits. Use it
to follow a moving node, a GPS tracker or someone else's badge, as long as that node broadcasts its
position. You need your own GPS fix for the direction and the distance.

Follow is the other half of this pair and does a different job: it walks you back along a track you
recorded yourself, while Tracker aims at a live node whose position arrives over the mesh and updates
as new packets come in.

## Choosing a node

Two ways to pick the node to follow:

- In the Nodes app, select a node and press F2 (Track). That sets it as the target and opens Tracker
- In Tracker, press F1 (Next node) to cycle through the nodes that have shared a position

If you have not chosen one, Tracker picks the first node that has a position.

## The needle turns with the compass and has no north-up mode

The needle points toward the node, turned into the direction you are facing. With the
[IMU compass](/had-badge-mod/apps/compass/) fitted and calibrated it is live while you stand still,
which is when you are actually pointing the badge at someone: turn on the spot and walk the way the
needle points. Tilting the badge does not move the needle, because the heading is tilt-compensated.

Tracker uses the compass whenever the compass has a heading to give. Without one it falls back to
your GPS course over ground while you move faster than 1 knot, and to north-up when you stop. A line
in the readout names the frame the needle is drawn in:

| Line | What has happened |
|------|-------------------|
| `(compass up, point the badge)` | The needle is drawn against the compass heading |
| `(relative to travel)` | No compass heading, so GPS course over ground |
| `(north up, move: no compass, walk to aim it)` | No compass to wait for: `imu_enabled` is off, nothing answered on the SAO bus at boot, or the magnetometer die is silent |
| `(north up, cal: sweep in the Compass app)` | The compass is sampling but nothing has been calibrated |
| `(north up, cal off: enable mag_cal_use in Settings)` | A calibration is saved but `mag_cal_use` is off, so no heading is published |
| `(north up, wait: no samples yet)` | The compass is fitted and calibrated, the samples are only late |

The last four are the four reasons a needle ends up drawn against true north, and each wants a
different action from you. The same four words appear on the Radar and Map labels and in
[Compass](/had-badge-mod/apps/compass/#why-a-view-is-showing-north).

The bearing printed next to the distance is always degrees true from you to the node, whatever the
needle is measured against.

## What the readout lists

Alongside the compass it lists what the node sent:

- Distance and bearing from you to the node
- The node's own speed in m/s and course in degrees, from its position broadcast
- Temperature in degrees C and humidity in percent, if the node sends environment telemetry
- Battery percentage, if the node sends device telemetry
- How long ago the last update arrived

## What a tracker needs to transmit

Anything that interoperates with Meshtastic works. The badge reads the standard `Position` app for
location, speed, and heading, and the `Telemetry` app for battery and environment metrics
(temperature, humidity, barometric pressure). A node that only sends text will not appear here until
it also shares a position.
