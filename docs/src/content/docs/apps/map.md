---
title: Map
description: A full-screen offline map centred on you, with your recorded trail and every positioned node on top.
---

Map draws the offline vector map full-screen with you at the centre. It is the roads-and-water
basemap from [Radar](/had-badge-mod/apps/radar/), but as a plain map you can read and pan, with your
recorded [Breadcrumbs](/had-badge-mod/apps/breadcrumbs/) trail and every positioned mesh node drawn on
top.

You need a GPS fix to place yourself. Without one the status line says
`No GPS fix. Press F3 to pan the map.` and you can still pan the loaded map area by hand.

## What it draws

- Roads dim and water in cyan, from the offline `/spiffs/map.vmap` file
- You, as an amber dot at your own position, which is the centre of the view unless you are panning
- Your trail, the breadcrumb track currently or last recorded, as an amber line
- Nodes that have reported a position, as small dots, the same set Radar plots

If no map file is uploaded yet, the status line adds `(no map)` and only you, the trail and the nodes
are drawn. See [Offline maps](/had-badge-mod/development/maps/) for how to build and upload a `.vmap`.

## Controls

| Key | Action |
|-----|--------|
| F1 | North-up / heading-up |
| F2 | Range: step the zoom through 100 m, 200 m, 500 m, 1 km, 2 km, measured centre to the nearest edge. It starts at 500 m |
| F3 | Pan / Center: toggle free panning. In pan mode the arrow keys move the view; press F3 again to snap back to your position |
| F5 / Esc | Back to the launcher |

## The map is north-up until you press F1

By default the map follows you and keeps north at the top, whether or not a compass is fitted, and in
north-up the compass is not consulted at all. In heading-up the map turns so the way ahead is at the
top, taken from the [IMU compass](/had-badge-mod/apps/compass/) when it is calibrated and fresh, so
the map orients while you stand still. Without a compass heading it falls back to GPS course over
ground while you move faster than 1 knot, and stays north-up otherwise. Radar, Tracker and Follow use
the same rule, except that Tracker and Follow have no north-up mode to hold them back.

The status line names the source, with the same labels the
[Radar](/had-badge-mod/apps/radar/) scope shows: `Heading up` for the compass, `Course up` for GPS
course, and `North up` with the reason in brackets when heading-up is selected but neither source is
usable. There are four such reasons, one per user action: `(move)` for no compass at all, `(cal)` for
nothing calibrated yet, `(cal off)` for a saved calibration switched off with `mag_cal_use`, and
`(wait)` for a compass that is running but has not produced a sample for more than 2 seconds. See
[Compass](/had-badge-mod/apps/compass/#why-a-view-is-showing-north) for what to do about each.

## Panning is always north-up

Press F3 to detach from your position and pan: the arrow keys scroll the view, so you can look ahead
along a route or check a junction. A press moves the view by half a screen radius. Panning ignores
F1 and stays north-up whether or not a compass is fitted, because a view that turns with your heading
while the arrows walk the centre away from you cannot be steered. The status line shows `Pan` while
you are panning. Press F3 again to re-centre on yourself. Panning works without a GPS fix too,
starting from the centre of the loaded map.

The map only repaints when the view actually moves, rotates or zooms, or when the trail or node set
changes, so it stays cheap even with a busy map.
