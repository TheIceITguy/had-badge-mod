# Project conventions

## UI text color (readability rule)

All on-screen text MUST stay readable against **every** background it can appear
over. Concretely:

- Use only **`theme.C_TEXT`** (primary) or **`theme.C_TEXT_DIM`** (secondary) for
  text. Both are light and readable on the whole dark palette (`C_BG`, `C_SURFACE`,
  `C_SURFACE_2`, `C_SIDEBAR`).
- The **footer / function-key label bar uses one fixed color (`C_TEXT`) always** —
  never color-coded per state, never varying. The bar itself is transparent.
- Do **not** put dark text on a dark surface, or color-code text by element in a
  way that some background makes unreadable.
- Accent color (`C_ACCENT`) is for **fills/indicators** (selection borders, the
  battery/mesh icons, my-message bubble background), not for body text.
- **Exception:** a dedicated fullscreen mode that intentionally takes a dark
  background (e.g. a chart or RSSI graph) may use its own scheme, but must keep
  high contrast.

## Status icons

Drawn from LVGL primitives (`lvgl.obj` rects), never from `lvgl.SYMBOL` glyph
fonts (not guaranteed compiled into this build). See `ui/icons.py`.

## Fonts

- Body/titles: `montserrat` (12/14/16/28/42 via `theme.f_*`).
- Footer / terminal-style HUD: `font_unscii_8` via `theme.f_term()` (old-school
  fixed-width).

## Navigation / keymap

`Enter` = activate/send, `Esc` (or `F5`) = back, arrows = navigate. F1–F5 are
labelled accelerators shown in the footer **without** the "F1".."F5" prefix and
**without** Up/Down (arrows do that). Every app must support `kb.esc() or kb.f5()`
for Back.

## App loop safety

`BaseApp.run()` wraps each tick in try/except: an error prints a traceback and
drops the app to the background (launcher takes over) instead of silently killing
the task. Keep that — it's how on-device LVGL issues surface.
