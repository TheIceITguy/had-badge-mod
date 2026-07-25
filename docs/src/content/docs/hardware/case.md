---
title: Case and enclosure
description: A 3D-printable badge case with a bay for the GPS module, and how to print and fit it.
---

`hardware/case/` holds a 3D-printable case for the Communicator badge: a remix of the stock case with
room for an ATGM336H GPS module and its wiring, so you can carry the badge with GPS attached instead
of leaving the receiver loose. The bay predates the move of the GPS default off J6 and fits either
routing.

## The file

`hardware/case/communicator-gps-case.stl` is the printable model. It was drawn in SketchUp and
exported as a binary STL with 2,846 triangles.

[Download the STL](https://github.com/giovi321/had-badge-mod/raw/main/hardware/case/communicator-gps-case.stl)

## Print it at full size in PLA or PETG

The model is in millimetres, so slice it at full size and do not scale it. The outer footprint is
about 148 x 175 mm and the walls stand 8.5 mm tall.

The STL carries no print settings of its own, so slice it for your own printer and filament. PLA or
PETG both work for a badge you carry indoors. Look at the orientation in your slicer preview before
you commit to supports.

## Wire the module to the SAO spare GPIO pair

The bay is sized for the common ATGM336H breakout. The firmware default is the SAO spare GPIO pair:
module TX to SAO_GPIO1 (GPIO7), module RX to SAO_GPIO2 (GPIO6), plus 3V3 and GND. GPS is on by
default, so a reboot after wiring is all it needs.

Neither expansion header is populated from the factory, so you solder the header or the wires
yourself. The J6 alternative, and the IO12 pad that is easy to misread as "IO10", are in the
[GPS app](/had-badge-mod/apps/gps/) page.
