---
title: Nodes
description: The list of mesh nodes the badge has heard, with distance, bearing and a heading-relative needle for each.
---

Nodes lists the other devices the badge has received packets from. Each row's first line shows the node
name, the last signal-to-noise ratio, and how long ago it was last heard. If the node has sent
telemetry, the battery level is added to the end of that line. With nothing heard yet the list reads
`No nodes heard yet`.

A node appears here after the badge decodes a packet from it, which includes its NodeInfo broadcast.
The name is the long name from NodeInfo when available, otherwise the short name, otherwise the node id
in `!aabbccdd` form.

## Position, distance, and bearing

When a node has shared its position, a second line shows its reported coordinates next to a GPS
marker. This appears whether or not the badge itself has a fix.

When the badge also has its own GPS fix, that line gains the distance to the node and the bearing as
degrees plus a compass point, for example `1.4km  312 NW`. That bearing is always degrees true.

## The row needle is turned into your heading

A small needle on the right of the row points at the same bearing, but turned into the direction you
are facing. It uses the [IMU compass](/had-badge-mod/apps/compass/) when that is calibrated and fresh,
your GPS course over ground while you move faster than 1 knot, and true north when neither is
available. There is no toggle, and nothing on the row says which of the three is in use, so
[Compass](/had-badge-mod/apps/compass/#why-a-view-is-showing-north) is where you find out why a needle
is pointing north.

With a compass fitted the needles re-aim as you turn on the spot, and the rows are not rebuilt to do
it, so the selection and the scroll position stay where they were. Turns under 4 degrees are ignored so
compass jitter does not repaint the list.

## Keys

Use the arrow keys to move through the list. With a node selected:

- F1 (Message) starts a direct message to it, switching to the Messages app with that node as the recipient
- F2 (Track) opens the [Tracker](/had-badge-mod/apps/tracker/) app pointed at that node, for following its live position, speed, and any temperature it reports
- F3 (Announ.) broadcasts this badge's own node info immediately, so the others learn about it without waiting for the periodic announce

The node table is held in RAM, holds 64 nodes, and evicts the least recently heard one when it is full.
