# Conventions

## Portable logic comes first

Protocol, crypto, packet, region, NMEA, compass maths, settings, node DB, dedup, and UI
layout/geometry are pure C11 with no ESP-IDF, LVGL, or FreeRTOS includes. They live in
`components/{core,mesh,util}` and the `portable/` subdirs of `net` and `ui`, and compile into
the firmware *and* into `host_tests/`. If logic can be tested on a PC, it lives here.

## Drivers own the hardware

`components/drivers` wraps esp_lcd, I2C, SPI, UART, LEDC, ADC, and PM. Everything else talks
to hardware only through these.

There is one source of truth for pins: `components/bsp/include/board_pins.h`.

## Only the `ui` task calls LVGL

Other tasks publish on the EventBus or push to a queue; the owning app drains it in its
`tick()`, which runs in the UI task. Never touch an `lv_obj_*` from the radio, keyboard, or
service tasks.

- EventBus handlers run on the publisher's stack: keep them short, non-blocking, no LVGL
- ISRs only give a semaphore or queue from `IRAM_ATTR` handlers; decoding happens in a task

## One font, one accent

- One font everywhere via `theme_font_body()` / `theme_font_title()`; apps must not set fonts
  directly
- One amber accent (`C_ACCENT`) on a dark surface; status colors only carry battery/signal
  semantics
- Icons are LVGL's built-in monochrome glyphs, recolored

All on-screen text must stay readable on every background it can appear on. Use the theme
color tokens, never hard-coded colors.

## Match the surrounding file

- C11, 4-space indent, K&R braces
- Static/file-local by default; expose the minimum through `include/<component>/...`
- Fixed-capacity buffers, no `malloc` in steady state (the radio/UI paths must not allocate)
- Functions return `esp_err_t` or a small int status; log with the component `TAG`
- Comments explain *why* and cite the ported source where relevant, not the obvious

## Meshtastic interop is load-bearing

The wire constants (channel hash `0x08`, flags `0x63`, nonce layout, sync word `0x2B`, region
frequencies) are frozen as host-test KATs in `host_tests/`. Change the codec only with a
passing test that pins the new bytes. A wrong byte silently breaks interop.

## NVS keys are 15 characters or fewer

That is a hardware limit. Labels can be long; keys can't. Everything is stored as a string;
typed access goes through `settings_get_*` / `settings_set_*`.

## Host tests gate every commit

Run `pwsh tools/run_host_tests.ps1` (or the CMake target) before every commit; it must be
green. It's the only pre-flash safety net. Run it from a shell where ESP-IDF has *not* been
activated: the runner calls `python -m ziglang cc`, and IDF's export script repoints `python`
at a virtualenv without that package.

Device behavior is validated with the bring-up checklist in the docs, under
`getting-started/bring-up`.

## Git

Commits are authored as `giovi321`. Keep the `upstream/` reference clone gitignored; it is not
part of this repo.
