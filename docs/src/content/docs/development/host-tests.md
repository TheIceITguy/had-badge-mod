---
title: Host tests
description: Run the 1113-check portable-logic suite on a PC, and avoid the ESP-IDF shell trap that breaks it.
---

Run this before every commit. It is the only part of the firmware you can verify without a
badge, and it is what stops a wrong byte in the Meshtastic codec from reaching the air.

```bash
python -m pip install ziglang
pwsh tools/run_host_tests.ps1
```

The runner compiles the portable sources and every test suite into one executable with
`zig cc`, a self-contained C compiler installed as a Python package, so no system toolchain is
needed. Current state: 42 sources, 18 suites, 1113 checks, 0 failures.

## Do not run it from an ESP-IDF shell

Open a clean shell for the host tests. ESP-IDF's export script repoints `python` at its own
virtualenv, which does not have `ziglang` installed, so `python -m ziglang cc` fails there even
though `pip install ziglang` succeeded earlier in your normal Python.

The symptom is a clean `idf.py build` followed by a host-test compile failure that has nothing
to do with the code you just changed. Nothing in the error names ESP-IDF, so it reads as a
broken test suite. Either use a second terminal, or install `ziglang` into the IDF virtualenv as
well.

## What the suite covers

The protocol, crypto, packet, region, NMEA, settings, node DB, dedup, compass, and UI layout
logic is plain C with no ESP-IDF or LVGL includes, so it compiles on a PC unchanged.

Meshtastic wire format, frozen as known-answer tests:

- AES against the FIPS-197 vectors, and the channel hash, nonce layout, and AES-CTR round trip
- Packet header build and parse, including the flags byte and the channel hint
- nanopb encode and decode, with a wire-format check against the original codec
- Region and frequency math, and `(from, id)` dedup

Navigation maths:

- NMEA sentence parsing, and the derived GPS state that the sidebar and the GPS app report
- Great-circle distance and bearing, the radar projection, the map projection with its
  rectangular line clip, and the `.vmap` reader
- The compass maths, 388 checks (see below)

Plumbing: the event bus, the settings registry, its JSON import and export, the node DB, and
the UI layout geometry.

## The compass checks

`host_tests/test_compass.c` is the new suite in this release and the only evidence behind the
compass, because the code path has not yet run against a real ICM-20948. It checks:

- The tilt-compensated heading against samples synthesised from a known attitude, and tilt
  invariance: 25 deg of roll must not move the needle. The check is proven to bite by failing
  the uncompensated version
- The reported roll and pitch signs, with nose up and left side up both positive
- Hard and soft-iron calibration recovered from a swept box, and the refusal of a sweep that
  only rocks the badge. Its per-axis spans clear the floor, and the box centre it would have
  saved is shown to swing a level heading by more than 20 deg
- Dropped magnetometer reads not being folded into a sweep, and the circular low-pass across
  the 0/360 seam
- The heading-source arbitration between compass, GPS course over ground, and north-up, plus
  the four north-up reasons with the exact label text each app shows

What this does not cover is the ICM-20948 transport and the axis transform between the
accelerometer and magnetometer dies. Those are device code, so they stay unproven until a badge
with a working sensor answers on the bus.

## Run with CMake instead

If you have gcc or clang and CMake, the same suite builds from the provided project:

```bash
cmake -B build-host host_tests
cmake --build build-host
ctest --test-dir build-host
```

`host_tests/CMakeLists.txt` lists the portable sources explicitly rather than globbing them, so
a new portable module has to be added there as well as picked up by the `zig cc` runner.
