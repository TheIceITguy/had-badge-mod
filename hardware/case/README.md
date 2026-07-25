# Communicator GPS case

A 3D-printable case for the Hackaday Communicator badge with a bay for the ATGM336H GPS module and
its wiring. The bay predates the move of the GPS default off J6 and fits either routing. It is a remix of the stock case, so the badge still fits the
same way; the change is the room it makes for the receiver.

Full documentation: [Case and enclosure](https://giovi321.github.io/had-badge-mod/hardware/case/) on
the docs site.

## Files

- `communicator-gps-case.stl`: the printable model, drawn in SketchUp and exported as a binary STL
  with 2,846 triangles

## Print at full size in PLA or PETG

The model is in millimetres, so slice it at full size with no scaling. Its outer footprint is about
148 x 175 mm and the walls are 8.5 mm tall.

There are no print settings baked into the STL, so slice it for your own printer and filament. PLA
or PETG is fine for a badge you carry around indoors. Check the orientation in your slicer preview
before you decide whether you need supports.

## Wire the module to the SAO spare GPIO pair

The bay is sized for the common ATGM336H breakout. Wire it to the SAO spare GPIO pair (module TX to
SAO_GPIO1 = GPIO7, module RX to SAO_GPIO2 = GPIO6, plus 3V3 and GND); GPS is on by default. Neither
expansion header is fitted from the factory, so you add the header or wires yourself.

The J6 alternative, which needs two settings changed, and the IO12-vs-IO10 silkscreen trap are
covered in the [GPS app docs](https://giovi321.github.io/had-badge-mod/apps/gps/).
