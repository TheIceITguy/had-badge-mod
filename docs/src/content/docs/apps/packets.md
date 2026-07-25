---
title: Packets
description: A rolling log of the last 32 mesh packets the badge received.
---

Packets is the view to open when you are not sure whether the radio is hearing anything. It lists the
last 32 packets the badge received, newest first, and each line has the time in UTC, the sender node id,
the port, and the received signal strength in dB. Empty, it reads `No packets yet`.

The port is the Meshtastic port number, shortened: `text`, `pos`, `info`, `route`, `telem`, `trace`, or
`app` for anything else.

If a specific device should be in range but nothing shows up, check that your region, channel, and key
match it. The log lives in RAM and resets on reboot.
