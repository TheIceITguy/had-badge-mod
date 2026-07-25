---
title: Offline maps
description: Build a .vmap vector map from OpenStreetMap and upload it for the radar, map, and trail overlays.
---

Roads and water can draw behind the [Radar](/had-badge-mod/apps/radar/),
[Map](/had-badge-mod/apps/map/) and [Breadcrumbs](/had-badge-mod/apps/breadcrumbs/) views once
you build a `.vmap` file on a PC and upload it to the badge over WiFi. There is no live download
on the badge; everything is offline.

The file lives on the badge's SPIFFS storage at `/spiffs/map.vmap`. The three apps share one
renderer (`components/ui/map_canvas.c`) that reads it directly and projects it with the same
range and bearing maths used for the radar blips, so the map, your trail, and other people stay
aligned.

A regional extract (a city centre a few kilometres across) is enough for radar ranges, which top
out at 5 km.

## Quickstart

Three steps:

1. Build a map for your area. Export it as GeoJSON from
   [Overpass Turbo](https://overpass-turbo.eu) (the query is in step 1 below), then convert it:
   ```sh
   python tools/osm2vmap.py --geojson area.geojson --out map.vmap
   ```
2. Upload it. Open `http://<badge-ip>/map` in a browser and pick `map.vmap`
3. Turn it on. Open Radar on the badge, wait for a GPS fix, and press `F4`

Roads and water now draw behind the radar blips. The one rule: the map has to cover the spot
where your GPS fix is, or there is nothing to draw. The rest of this page explains each step and
the tuning flags.

## 1. Export roads and water from OpenStreetMap

The simplest source is [Overpass Turbo](https://overpass-turbo.eu). Pan and zoom to your area,
paste this query (it pulls roads and water for the current map view), run it, then use Export,
"download as GeoJSON":

```overpassql
[out:json][timeout:60];
(
  way["highway"]({{bbox}});
  way["natural"="water"]({{bbox}});
  way["natural"="coastline"]({{bbox}});
  way["waterway"]({{bbox}});
  relation["natural"="water"]({{bbox}});
);
out geom;
```

Any tool that produces GeoJSON works, for example running a downloaded `.osm` extract through
[`osmtogeojson`](https://github.com/tyrasd/osmtogeojson).

## 2. Convert the GeoJSON to .vmap

```sh
python tools/osm2vmap.py --geojson area.geojson --out map.vmap
```

Useful flags:

- `--simplify <metres>`, the Douglas-Peucker tolerance (default `2`). Larger values drop more
  vertices and shrink the file
- `--bbox minlon,minlat,maxlon,maxlat`, keep only features intersecting this box, to trim a large
  export down to your area

The tool classifies OSM tags into two layers (`highway=*` to roads, `natural=water|coastline`,
`waterway=*` and `water=*` to water), simplifies each way, splits long ways so no feature exceeds
256 points, and writes the binary. It prints the feature counts and size, and re-parses its own
output as a sanity check. It needs only the Python standard library.

Keep the result small. A soft budget of 1 to 2 MB leaves plenty of room on the roughly 9.6 MB
SPIFFS partition. If a file is too big, raise `--simplify` or narrow `--bbox`.

## 3. Upload over WiFi

Join the badge's WiFi and web UI (see [WiFi and web UI](/had-badge-mod/connectivity/wifi/)), open
`http://<badge-ip>/map`, pick your `map.vmap`, and upload. The badge streams it to a temp file,
validates the header, and atomically swaps it in, so a bad or interrupted upload leaves the
previous map untouched. Then open Radar and press `F4` to toggle the overlay.

Uploading a new file replaces the old one. The map survives reboots but, like all SPIFFS data, is
wiped if the partition table is ever changed (a one-time cable re-flash event).

## File format reference

`.vmap` is little-endian throughout. Coordinates are fixed-point 1e-7 degrees
(`int32 = round(deg * 1e7)`). The canonical definition lives in `components/util/include/util/vmap.h`; the
reader and host tests are in `components/util/vmap.c` and `host_tests/test_map.c`.

Header, 28 bytes:

| bytes | field |
| ----- | ----- |
| 4 | magic `VMAP` |
| 1 | version (`1`) |
| 1 | flags (`0`) |
| 2 | reserved |
| 16 | global bbox: `min_lat, min_lon, max_lat, max_lon` (int32 e7) |
| 4 | feature count (uint32) |

Each feature, a 20-byte prefix plus 8 x N point bytes:

| bytes | field |
| ----- | ----- |
| 1 | class (`1` = road, `2` = water) |
| 1 | reserved |
| 2 | point count N (uint16, 2 to 256) |
| 16 | feature bbox (int32 e7) |
| 8 x N | points: `lat_e7, lon_e7` pairs (int32), in polyline order |

The per-feature bounding box lets the badge skip out-of-view features without reading their
points, so it only projects the geometry near you. Unknown class bytes are skipped, so the format
can grow new layers without breaking older firmware.
