"""Status icons drawn from LVGL primitives (rects), robust on this frozen build
(no SYMBOL fonts). Each icon is a small obj 'canvas' with child rects; set_state()
only recolors/resizes existing rects (cheap, alloc-free) so it is safe to call
from event handlers.
"""
import lvgl

from ui import theme

_hx = lvgl.color_hex


def _canvas(parent, w, h):
    o = lvgl.obj(parent)
    o.set_size(w, h)
    o.set_scrollbar_mode(0)
    o.set_style_pad_all(0, 0)
    o.set_style_border_width(0, 0)
    o.set_style_radius(0, 0)
    o.set_style_bg_opa(0, 0)
    return o


def _rect(parent, x, y, w, h, color=None, radius=0):
    r = lvgl.obj(parent)
    r.set_pos(x, y)
    r.set_size(w, h)
    r.set_scrollbar_mode(0)
    r.set_style_pad_all(0, 0)
    r.set_style_border_width(0, 0)
    r.set_style_radius(radius, 0)
    if color is None:
        r.set_style_bg_opa(0, 0)
    else:
        r.set_style_bg_opa(255, 0)
        r.set_style_bg_color(_hx(color), 0)
    return r


def _fill(r, color):
    r.set_style_bg_opa(255, 0)
    r.set_style_bg_color(_hx(color), 0)


def _hide(r):
    r.set_style_bg_opa(0, 0)


class BatteryIcon:
    INNER_MAX = 14

    def __init__(self, parent):
        self.o = _canvas(parent, 22, 16)
        self.body = _rect(self.o, 0, 2, 18, 12, None, radius=3)
        self.body.set_style_border_width(1, 0)
        self.body.set_style_border_color(_hx(theme.C_TEXT_DIM), 0)
        self.nub = _rect(self.o, 18, 5, 3, 6, theme.C_TEXT_DIM, radius=1)
        self.fill = _rect(self.o, 2, 5, 8, 6, theme.C_OK, radius=1)
        self.dash = _rect(self.o, 6, 7, 6, 2, theme.C_TEXT_MUTE, radius=1)
        self.set_state(None, False, True, False)

    def set_state(self, pct, charging, usb, present):
        state = theme.battery_state(pct, charging, usb, present)
        if state == "none":
            self.body.set_style_border_color(_hx(theme.C_TEXT_MUTE), 0)
            _fill(self.nub, theme.C_TEXT_MUTE)
            _hide(self.fill)
            _fill(self.dash, theme.C_TEXT_MUTE)
            return
        self.body.set_style_border_color(_hx(theme.C_TEXT_DIM), 0)
        _fill(self.nub, theme.C_TEXT_DIM)
        _hide(self.dash)
        color = theme.battery_fill_color(pct, charging, present) or theme.C_OK
        units = theme.battery_fill_units(pct, self.INNER_MAX)
        self.fill.set_width(max(1, units))
        _fill(self.fill, color)


class WifiIcon:
    def __init__(self, parent):
        self.o = _canvas(parent, 18, 14)
        self.bars = [
            _rect(self.o, 1, 9, 4, 5, theme.C_IDLE, radius=1),
            _rect(self.o, 7, 5, 4, 9, theme.C_IDLE, radius=1),
            _rect(self.o, 13, 1, 4, 13, theme.C_IDLE, radius=1),
        ]
        self.set_state("off", None)

    def set_state(self, state, rssi):
        level = theme.wifi_level(state, rssi)
        col = theme.wifi_color(state)
        for i, bar in enumerate(self.bars):
            _fill(bar, col if i < level else theme.C_IDLE)


class MeshIcon:
    def __init__(self, parent):
        self.o = _canvas(parent, 18, 14)
        self.dots = [
            _rect(self.o, 1, 5, 4, 4, theme.C_IDLE, radius=2),
            _rect(self.o, 7, 5, 4, 4, theme.C_IDLE, radius=2),
            _rect(self.o, 13, 5, 4, 4, theme.C_IDLE, radius=2),
        ]
        self.note = _rect(self.o, 14, 0, 4, 4, theme.C_ACCENT, radius=2)
        _hide(self.note)
        self.set_state(False, 0)

    def set_state(self, backend_up, peers, tx=False):
        level = theme.mesh_level(backend_up, peers)
        for i, dot in enumerate(self.dots):
            if not backend_up:
                _fill(dot, theme.C_IDLE)
            elif i == 0 and peers <= 0:
                _fill(dot, theme.C_ACCENT)
            else:
                _fill(dot, theme.C_OK if i < level else theme.C_IDLE)

    def notify(self, on):
        if on:
            _fill(self.note, theme.C_ACCENT)
        else:
            _hide(self.note)


class GpsIcon:
    def __init__(self, parent):
        self.o = _canvas(parent, 14, 14)
        self.ring = _rect(self.o, 1, 1, 12, 12, None, radius=6)
        self.ring.set_style_border_width(2, 0)
        self.ring.set_style_border_color(_hx(theme.C_TEXT_MUTE), 0)
        self.dot = _rect(self.o, 5, 5, 4, 4, theme.C_OK, radius=2)
        self.set_state("off")

    def set_state(self, state):
        if state == "off":
            self.ring.set_style_border_color(_hx(theme.C_TEXT_MUTE), 0)
            _hide(self.dot)
        elif state == "search":
            self.ring.set_style_border_color(_hx(theme.C_WARN), 0)
            _hide(self.dot)
        elif state == "fix2d":
            self.ring.set_style_border_color(_hx(theme.C_WARN), 0)
            _fill(self.dot, theme.C_WARN)
        else:  # fix3d
            self.ring.set_style_border_color(_hx(theme.C_OK), 0)
            _fill(self.dot, theme.C_OK)


def app_icon(parent, color, size=34):
    """A flat rounded color chip (no gradient, no letter) for launcher tiles."""
    g = lvgl.obj(parent)
    g.set_size(size, size)
    g.set_scrollbar_mode(0)
    g.set_style_pad_all(0, 0)
    g.set_style_border_width(0, 0)
    g.set_style_radius(theme.R_CARD, 0)
    g.set_style_bg_opa(255, 0)
    g.set_style_bg_color(_hx(color), 0)
    # subtle flat inner notch for a modern look (small darker square, no gradient)
    inner = lvgl.obj(g)
    inner.set_scrollbar_mode(0)
    inner.set_size(size // 3, size // 3)
    inner.set_style_border_width(0, 0)
    inner.set_style_radius(size // 6, 0)
    inner.set_style_bg_opa(60, 0)
    inner.set_style_bg_color(_hx(0x000000), 0)
    inner.align(lvgl.ALIGN.CENTER, 0, 0)
    return g
