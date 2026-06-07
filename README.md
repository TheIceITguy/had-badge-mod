# had-badge-mod

A modular firmware mod for the **Hackaday Communicator Badge** (ESP32-S3 + SX1262 LoRa,
running `lvgl_micropython`). It turns the stock single-purpose firmware into a small
"OS" with a service layer, an event bus, a paged app launcher, a schema-driven settings
system, a WiFi WebUI, GPS support, power management — and, most importantly,
**true Meshtastic interoperability** so the badge can chat with and share its GPS
position to real Meshtastic devices and the Meshtastic phone app.

Project repo: <https://github.com/giovi321/had-badge-mod>

> **Status:** under active development, milestone by milestone (see Roadmap). The
> Meshtastic protocol stack and other host-testable logic are validated with `pytest`
> in `firmware/tests/`; on-device behavior is verified by flashing a badge.

## Hardware target

- **MCU:** ESP32-S3 WROOM, 8 MB PSRAM / 16 MB flash
- **Radio:** Semtech SX1262 (LoRa)
- **Display:** NV3007 TFT 428×142 (via LVGL)
- **Input:** TCA8418 I²C keyboard matrix
- **Power:** LiPo + MCP73831 charger
- **Expansion:** header J6 — `VCC (3V3) / IO11 / IO12 / GND`. An **ATGM336H** GPS
  module (NMEA over UART) wires here: GPS **TX → IO12** (ESP32 RX), GPS **RX → IO11**
  (ESP32 TX, optional), VCC → 3V3, GND → GND.

## Layout

```
firmware/badge/        # the deployable MicroPython tree (sync this to the badge)
  core/                # services registry, event bus, settings schema, app manifest
  net/                 # LoRa driver + backend abstraction
    mesh/              # Meshtastic protocol stack (protobuf, AES-CTR, packet, regions)
  services/            # gps, time, mesh node DB, wifi, web, battery
  apps/                # launcher, settings, messaging, GPS apps, + stock apps
  ui/ hardware/ libs/  # display/keyboard/LVGL helpers, KV store, vendored libs
firmware/tests/        # host-side CPython tests (NOT deployed)
firmware/scripts/      # deploy + on-device probe helpers
```

## Build / deploy

The firmware is MicroPython; you sync the `firmware/badge/` tree onto a badge that is
already flashed with the `lvgl_micropython` image. From `firmware/`:

```bash
python -m venv venv && source venv/bin/activate    # (Windows: venv/Scripts/activate)
pip install -r requirements.txt                     # mpremote, etc.

# Copy everything to the badge and reset it
mpremote cp -r badge/* :
# or, selective sync that also deletes removed files:
scripts/update.py --reset push
```

The full `lvgl_micropython` `.bin` is flashed over USB with `esptool`/`mpremote`
(not part of this repo — see the upstream project). WebUI firmware updates in this mod
replace **`.py` files only** and reboot; they do not reflash the MicroPython runtime.

## Host-side tests

```bash
cd firmware && pip install pytest cryptography && pytest tests/ -q
```

Tests cover the protocol-critical, hardware-independent code (Meshtastic protobuf,
AES-CTR, channel hashing, packet build/parse, region/frequency math, NMEA parsing).
MicroPython-only modules (`machine`, `lvgl`, `btree`, `network`, `uasyncio`) are stubbed
in `firmware/tests/shims/`.

## On-device capability probe

Before relying on WiFi or Meshtastic crypto, run the probe on a badge to confirm the
build's capabilities:

```bash
mpremote run firmware/scripts/probe_device.py
```

It reports whether `network` (WiFi) is present, whether AES-CTR is available via
`cryptography`, UART/ADC availability, the node id from `machine.unique_id()`, and free
heap with LVGL up.

## Roadmap

| Milestone | Scope | State |
|-----------|-------|-------|
| M0 | Bootstrap repo + scaffold | ✅ |
| M1 | Meshtastic codec + crypto (host-tested) | ✅ |
| M2 | Core OS layer (services, events, settings, manifest) | ✅ |
| M3 | Network backend abstraction + BadgeNet adapter | ✅ |
| M4 | Meshtastic backend live (interop) | ✅ |
| M5 | GPS + time service | ✅ |
| M6 | Position sharing + node DB + node map | ✅ |
| M-Name | Device name as single source of truth | ✅ |
| M-Brand | De-brand + repo links | ✅ |
| M7 | WiFi + WebUI (`.py` updates) | ⏳ |
| M8 | Paged launcher + settings app + app SDK | ⏳ |
| M9 | Power management + battery % on all screens | ⏳ |

## Credits & license

Based on the open-source [Hackaday Communicator Badge firmware](https://github.com/Hack-a-Day/2025-Communicator_Badge)
(upstream pinned at commit `86afce9`). This fork removes event-specific branding and adds
the features above. Original license retained in [`LICENSE.txt`](LICENSE.txt).
