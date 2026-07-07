# Internal addon board

A small PCB that mounts on the back (battery side) of the Communicator badge main board. It solders onto the two expansion headers J8 (SAO) and J6 (GPS/expansion) and breaks them out to JST connectors, so internal peripherals can be plugged instead of soldered. The lower part of the board is a plain tab that gets hot-glued to the badge.

Full documentation: [Internal addon board](https://giovi321.github.io/had-badge-mod/hardware/internal-addon-board/) on the docs site.

All badge geometry in this folder is extracted from the official KiCad design at [Hack-a-Day/2025-Communicator_Badge](https://github.com/Hack-a-Day/2025-Communicator_Badge) (`hardware/communicator_pcb/communicator_pcb.kicad_pcb`, MIT license) and x-mirrored to the back view. Nothing is drawn by eye.

## Files

| File | What it is |
|------|------------|
| `internal-addon-board.fzz` | The Fritzing sketch of the addon board |
| `board-shape.svg` | Custom board outline (rev C, 42.0 x 37.5 mm) to load as the PCB shape in Fritzing |
| `placement-overlay.png` | The outline drawn over the badge back with corner coordinates, for visual verification |
| `communicator-badge-addon-internal.zip` | Gerber export ready for the fab |
| `gerber/` | The same gerbers, unzipped |
| `fritzing-parts/communicator-badge-mainboard.fzpz` | Badge part, cluster variant: PCB view holds only the J8+J6 hole pattern. Use this as the electrical part in the sketch |
| `fritzing-parts/communicator-badge-full.fzpz` | Badge part, full-board variant: PCB view holds the whole 148 x 81 mm badge with outline, mounting holes and component reference silk. Use for mechanical reference, remove before gerber export |
| `fritzing-parts/src/` | SVG and fzp sources of both parts |

## Connector pinout

Both headers are 2.54 mm pitch through-holes, unpopulated on the stock badge. Both VCC pins are the same 3.3 V rail; there is no 5 V on either header. GPIO numbers verified against the badge firmware and the back silkscreen.

| Connector | Pin | Net | ESP32-S3 GPIO |
|-----------|-----|-----|---------------|
| J8 (SAO v2) | 1 | VCC 3V3 | - |
| J8 | 2 | GND | - |
| J8 | 3 | SAO_SDA | 4 |
| J8 | 4 | SAO_SCL | 5 |
| J8 | 5 | SAO_GPIO1 | 7 |
| J8 | 6 | SAO_GPIO2 | 6 |
| J6 | 1 | 3V3 | - |
| J6 | 2 | UART TX out of badge | 11 |
| J6 | 3 | UART RX into badge | 12 |
| J6 | 4 | GND | - |

Hole sizes: J8 drill 1.016 mm, pad 1.727 mm. J6 drill 1.0 mm, pad 1.7 mm.

## Hole coordinates (badge back view)

Origin = top-left corner of the badge as seen from the back, x right, y down, mm. In this view J6 sits left of J8 and J8 pin 1 (VCC) is the rightmost column of the 2x3 field.

| Hole | x | y |
|------|-------|-------|
| J6-1 | 28.420 | 6.040 |
| J6-2 | 28.420 | 8.580 |
| J6-3 | 28.420 | 11.120 |
| J6-4 | 28.420 | 13.660 |
| J8-5 | 35.278 | 9.596 |
| J8-6 | 35.278 | 12.136 |
| J8-3 | 37.818 | 9.596 |
| J8-4 | 37.818 | 12.136 |
| J8-1 | 40.358 | 9.596 |
| J8-2 | 40.358 | 12.136 |

J6-1 relative to J8-1: (-11.938, -3.556) mm = 4.7 x 1.4 pitches of 2.54 mm. The headers do not share a 0.1 inch grid; lay out on a 0.254 mm (10 mil) grid, where the offset is a clean 47 x 14 units.

## Board outline (rev C)

42.0 x 37.5 mm, twelve corners, badge back-view coordinates, clockwise:

(24.3, 2.5) (44.5, 2.5) (44.5, 14.6) (38.5, 14.6) (38.5, 29.9) (33.6, 29.9) (33.6, 40.0) (2.5, 40.0) (2.5, 31.5) (9.0, 31.5) (9.0, 22.5) (24.3, 22.5)

Edge clearances to the components the outline dodges: SX1262 0.34 mm, RST button 0.52 mm, battery JST 0.30 mm, USB-C 0.41 mm, SMA 3.8 mm. Typical fab outline tolerance is +-0.2 mm, so the tightest gaps can shrink to about 0.1 mm; the board is glued and soldered, so this works, but dry-fit a 100% scale print of `board-shape.svg` before ordering.

## Fritzing placement

Board bounding-box origin sits at badge back-view (2.5, 2.5). With the custom shape loaded as the PCB:

- cluster part at board x + 21.0, board y - 0.5 (its J6/J8 holes then land exactly on the headers)
- full-board part, if used as reference, at board x - 2.5, board y - 2.5

J6-1 lands at board-local (25.920, 3.540), J8-1 at (37.858, 7.096).

## Regenerating

Parse `communicator_pcb.kicad_pcb` for the J6/J8 footprints (pad `at` plus footprint origin) and the `Edge.Cuts` graphics, mirror x about the board bounding box, flip SVG arc sweep flags, rebuild the SVGs. All values above are exact file values, no rounding beyond 3 decimals.
