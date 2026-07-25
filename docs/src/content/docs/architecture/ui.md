---
title: User interface
description: The layout constants, the theme accessors, and the navigation model apps have to follow.
---

Apps draw inside a fixed frame they do not own, and they take fonts and colors from the theme
rather than setting their own. The chrome (sidebar and bottom bar) is built once at boot and
lives on LVGL's top layer, so an app screen swap never has to redraw it.

## Layout

The screen is 428 by 142 in landscape. Three regions make up the chrome:

- A status sidebar 28 pixels wide on the left, stopping where the bottom bar begins, so its
  height is the screen height minus the bottom bar
- A function-key bar across the full width of the bottom, 18 pixels tall, running under the
  sidebar to the left edge, with five evenly distributed cells whose labels are clipped with an
  ellipsis so text never runs off the screen
- A per-app area between the header and the bottom bar, to the right of the sidebar

Geometry constants live in `components/ui/include/ui/layout.h` and the host tests check the
arithmetic: the five bottom-bar cells sum to the screen width, the sidebar height is correct,
and so on.

## Colors and fonts come from the theme

Colors come from `ui/colors.h`. The font is Montserrat at 14 for body text and 18 for titles,
exposed through `theme_font_body()` and `theme_font_title()`. Apps call those accessors instead
of setting fonts, and use the color tokens instead of hex literals, because a hard-coded color
is what breaks readability when the surface behind it changes.

## Status sidebar

The sidebar shows battery, mesh, WiFi, and GPS. Each icon is recolored from a state value: the
mesh icon lights when the backend is up, and the battery icon picks a color from the charge
level. The mapping functions are pure and host tested.

## Navigation

The launcher is a horizontally scrolling strip of icon tiles. Left and Right move the selection
and scroll the focused tile into view. Enter or F1 (Open) opens the focused app, and the
launcher returns to the tile you last opened. The home screen also shows the firmware version
in the top bar, small and left-aligned, for example `v1.0.0`.

`launcher.c` holds the tile array at `TILE_MAX` 16 against 12 registered apps. Keep that
headroom: the strip scrolls, so extra tiles cost only the array, while a cap at or below the
app count drops the tail of the list off the home screen with no error anywhere. Radar and Map
were unreachable that way while the cap was 8.

Inside an app, Esc or F5 goes back, and F5 always reads "Back" so the control stays visible.
F1 to F4 are app specific and the bottom bar shows their current labels.

Apps that set `autohide_bar` (Messages) hide the bottom bar after a delay. The first F1 to F4
press then only reveals the bar so the labels can be read; a second press runs the function.
Back still exits in one press.

Text entry uses a one-line input. In the Messages app, Enter sends and the Up and Down arrows
scroll the chat history while the input keeps focus.
