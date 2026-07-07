---
title: Internal addon board
description: A back-side PCB that breaks out J6 and J8 to JST connectors for internal peripherals.
---

The `hardware/internal-addon-board/` folder holds a small PCB that mounts on the back of the badge main board. It solders onto the two expansion headers, J8 (the SAO port) and J6 (the GPS/expansion header), and breaks them out to JST connectors. Internal peripherals, a GPS module or an IMU for example, then plug in instead of being soldered to loose wires. The lower part of the board is a plain tab that gets hot-glued to the badge, so the whole thing rides inside the case.

The board outline and every hole position come from the official badge KiCad design ([Hack-a-Day/2025-Communicator_Badge](https://github.com/Hack-a-Day/2025-Communicator_Badge), MIT), mirrored to the back view. Nothing is traced from photos.

## What it connects to

Both headers are 2.54 mm through-holes, empty on the stock badge. Both VCC pins are the same 3.3 V rail. There is no 5 V on either header, and the ESP32-S3 pins are not 5 V tolerant, so every peripheral on the addon board must be a 3.3 V device.

| Connector | Pin | Signal | ESP32-S3 GPIO |
|-----------|-----|--------|---------------|
| J8 (SAO) | 1 | 3V3 | - |
| J8 | 2 | GND | - |
| J8 | 3 | I2C SDA | 4 |
| J8 | 4 | I2C SCL | 5 |
| J8 | 5 | GPIO1 | 7 |
| J8 | 6 | GPIO2 | 6 |
| J6 | 1 | 3V3 | - |
| J6 | 2 | UART TX (badge out) | 11 |
| J6 | 3 | UART RX (badge in) | 12 |
| J6 | 4 | GND | - |

J6 is the same header the [GPS](/had-badge-mod/apps/gps/) uses, so a GPS module can move onto the addon board and keep working with the firmware unchanged. The J8 I2C bus and the two GPIOs are free for new hardware.

The 3.3 V rail is sized for the badge itself. Small sensors are fine; power-hungry modules are not. If something genuinely needs 5 V, put a boost converter on the addon board.

## The shape

The outline is a 42.0 x 37.5 mm polygon that covers the two headers and the flat passive areas, and dodges everything tall or that you still need to reach: the SX1262 radio module, the RST button, the USB-C connector, the battery JST and the SMA antenna. The attenuator bypass jumper JP1 also stays uncovered. `placement-overlay.png` shows the outline drawn over the badge back with all corner coordinates.

Edge clearances are tight by design: SX1262 0.34 mm, RST 0.52 mm, battery JST 0.30 mm, USB-C 0.41 mm. Fabs hold the outline to about +-0.2 mm, so the tightest gaps can come back near 0.1 mm. That is fine for a board that is soldered and glued in place, but print `board-shape.svg` at 100% scale and lay it on the badge before ordering. If it rubs, a few strokes of sandpaper on the SX1262 or battery-side edge fix it.

One thing the KiCad file does not contain is component heights. The areas the board covers hold only passives and SOT-23 packages by footprint, which normal 2.5 mm header standoff clears, but verify on your badge during the dry fit. Also check the board against the [printed case](/had-badge-mod/hardware/case/) if you use it, since both live on the back of the badge.

## Designing in Fritzing

Everything needed is in `hardware/internal-addon-board/`:

1. Import the two badge parts (File > Open on each `.fzpz` in `fritzing-parts/`). Both show the badge from the back
2. In PCB view, select the board, and in the Inspector load `board-shape.svg` as a custom shape
3. Place the cluster part at board x + 21.0, board y - 0.5. Its ten holes then sit exactly on J6 and J8. Lock it
4. Optionally place the full-board part at board x - 2.5, board y - 2.5 as a live picture of the badge underneath. Delete it before exporting gerbers, its reference silkscreen would print on the addon board
5. Set the grid to 0.254 mm. The two headers are 4.7 x 1.4 pitches apart, so they never both land on a 2.54 mm grid
6. Route with wide traces (12 to 16 mil signals, 20 mil and up for 3V3 and GND) and add a ground fill on the bottom

## Fabrication

Any standard service handles this board. On the PCBWay order form pick the standard 6/6 mil track/spacing class, not a finer one; the routed board does not go below 10 mil anywhere and finer classes only add cost. Drills are 1.0 and 1.016 mm with 1.7 mm pads, no slots, single lamination. The gerbers that went to the fab are in the folder, both zipped (`communicator-badge-addon-internal.zip`) and unzipped (`gerber/`).

## Assembly

1. Solder standoff pin headers into J6 and J8 from the back of the badge
2. Seat the addon board on the pins, check it floats level and touches nothing, then solder
3. Press RST once to confirm the button survives with the board in place
4. Hot-glue the bottom tab to the badge

The Fritzing sketch is `internal-addon-board.fzz`; the folder [README](https://github.com/giovi321/had-badge-mod/tree/main/hardware/internal-addon-board) carries the full coordinate tables for reworking the outline or the placement.
