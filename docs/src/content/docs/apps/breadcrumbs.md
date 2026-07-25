---
title: Breadcrumbs and Follow
description: Record a GPS track, then navigate back along it with a compass needle.
---

These two apps work together. Breadcrumbs records where you go, and Follow points you back. Both need
GPS wired and a fix.

## Breadcrumbs

Open Breadcrumbs and press F1 to start recording. The badge writes the current position to a file on
its storage partition, named from the start time, for example `trk_20260609-143022.csv`. It samples a
point once you have moved 8 m, or after 30 seconds if you have not, so a stationary badge does not
fill the file. The status line reads `Recording  N pts` with the file name below it, and a red dot in
the status sidebar shows that a track is recording. Press F1 again to stop.

Recording needs the clock set, which on this badge means a GPS fix has been seen since boot. Without
one F1 refuses and says `Cannot start: the clock is not set. Enable GPS and wait for a fix first.`

The file is a CSV: two comment lines, then one row per point with a timestamp, latitude, longitude,
altitude, and satellite count.

Press F2 to switch between the text view and a map view of the trail: the recorded breadcrumbs are
drawn as a line over the offline roads-and-water map, centred on your current position (or the last
recorded point), with nearby nodes marked. Press F2 again to return to the text view. The map needs an
uploaded `.vmap` to show roads, see [Offline maps](/had-badge-mod/development/maps/), but the trail
itself draws either way. The scale is fixed at 500 m from the centre to the nearest edge.

### The trail map is always north-up

It stays north-up even with the IMU compass fitted, and has no orientation toggle by design: the trail
then has the same shape every time you open it, which is what you want when you are comparing it
against a map. For a heading-up view, or to pan and zoom, use the [Map](/had-badge-mod/apps/map/) app.

## Follow

Follow loads the most recent track and shows a needle pointing toward the next waypoint on the way
back to the start, with the distance to that waypoint and which one you are on. It advances to the
previous waypoint once you are within 12 m of the current one, walking you back to where the track
began, and says `Back at the start.` when you get there. F1 reloads the track file, for when you have
just recorded a new one.

The needle is turned into the direction you are facing. Follow has no north-up mode, so with the
[IMU compass](/had-badge-mod/apps/compass/) calibrated it works standing still, which is when you are
reading the dial. Without a compass heading it uses GPS course over ground while you move faster than
1 knot, and when you stop there is no course, so it falls back to north up. The last line of the
readout names the frame the needle is drawn in:

| Line | What has happened |
|------|-------------------|
| `compass up, point the badge` | The needle is drawn against the compass heading |
| `relative to travel` | No compass heading, so GPS course over ground |
| `north up, move: no compass, walk to aim the needle` | No compass to wait for: `imu_enabled` is off, nothing answered on the SAO bus at boot, or the magnetometer die is silent |
| `north up, cal: sweep the badge in the Compass app` | The compass is sampling but nothing has been calibrated |
| `north up, cal off: enable mag_cal_use in Settings` | A calibration is saved but `mag_cal_use` is off, so no heading is published |
| `north up, wait: the compass has no samples yet` | The compass is fitted and calibrated, the samples are only late |

The four cause words are the ones Radar, Map and
[Compass](/had-badge-mod/apps/compass/#why-a-view-is-showing-north) use. The bearing printed above the
line is degrees true either way.
