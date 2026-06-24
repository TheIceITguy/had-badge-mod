---
title: Map
description: A full-screen offline map centered on you, with your trail and nearby nodes.
---

Map draws the offline vector map full-screen with you at the centre. It is the roads-and-water
basemap from [Radar](/apps/radar/), but as a plain map you can read and pan, with your recorded
[Breadcrumbs](/apps/breadcrumbs/) trail and every positioned mesh node drawn on top.

You need a GPS fix to place yourself. Without one the map says so, but you can still press **F3**
to pan around the loaded map area by hand.

## What it draws

- **Roads** dim and **water** in cyan, from the offline `/spiffs/map.vmap` file.
- **You** as an amber dot at the centre (your real position, even while panning).
- **Your trail** — the breadcrumb track currently or last recorded — as an amber line.
- **Nodes** that have reported a position, as small dots, the same set Radar plots.

If no map file is uploaded yet, the status line shows `(no map)` and only you, the trail and the
nodes are drawn. See [Offline maps](/development/maps/) for how to build and upload a `.vmap`.

## Controls

| Key | Action |
|-----|--------|
| F1 | North-up / heading-up. North-up keeps north at the top; heading-up turns the map so your direction of travel is up while you move (from GPS course, until the badge gains a magnetometer). |
| F2 | Range — step the zoom: 100 m, 200 m, 500 m, 1 km, 2 km (centre to the nearest edge). |
| F3 | Pan / Center — toggle free panning. In pan mode the arrow keys move the view; press F3 again to snap back to your position. |
| F5 / Esc | Back to the launcher. |

## North-up, heading-up and panning

By default the map follows you. Standing still it is north-up; once you move it can switch to
heading-up so the way ahead is at the top — the same orientation rule the Radar and Tracker apps
use. When the badge gains a magnetometer this heading will come from a real compass, so the map
orients even while you stand still.

Press **F3** to detach from your position and pan: the arrow keys scroll the view (north stays
up), so you can look ahead along a route or check a junction. Press **F3** again to re-centre on
yourself. Panning works without a GPS fix too, starting from the centre of the loaded map.

The map only repaints when the view actually moves, rotates or zooms, or when the trail or node
set changes, so it stays cheap even with a busy map.
