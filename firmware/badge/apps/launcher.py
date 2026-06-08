"""Home launcher: a row of app tiles, keyboard-navigated.

Arrows move the selection, Enter opens, F1..F5 are accelerators. When the running
app backgrounds (Esc/F5), the launcher re-foregrounds itself (like the old menu).
"""
import lvgl

from apps.base_app import BaseApp
from ui.frame import Frame
from ui import icons, theme

_hx = lvgl.color_hex


class Launcher(BaseApp):
    def __init__(self, name, badge, apps):
        super().__init__(name, badge)
        self.apps = [a for a in apps if a]
        self.foreground_sleep_ms = 40
        self.background_sleep_ms = 150
        self.fr = None
        self.tiles = []
        self.sel = 0

    def _build_tiles(self):
        body = self.fr.body
        n = max(1, len(self.apps))
        inner_w = theme.CONTENT_W - 2 * theme.PAD_M
        gap = 8
        tile_w = min(92, max(56, (inner_w - (n - 1) * gap) // n))
        tile_h = 84
        total = n * tile_w + (n - 1) * gap
        start_x = max(0, (inner_w - total) // 2)
        self.tiles = []
        for i, app in enumerate(self.apps):
            x = start_x + i * (tile_w + gap)
            tile = lvgl.obj(body)
            tile.set_scrollbar_mode(0)
            tile.add_style(theme.st_card, 0)
            tile.set_pos(x, 2)
            tile.set_size(tile_w, tile_h)
            g = icons.app_glyph(tile, app.name, 40)
            g.align(lvgl.ALIGN.TOP_MID, 0, 2)
            name = lvgl.label(tile)
            name.set_text(app.name)
            name.set_style_text_font(theme.f_tiny(), 0)
            name.set_style_text_color(_hx(theme.C_TEXT), 0)
            name.set_style_text_align(lvgl.ALIGN.CENTER, 0)
            name.set_width(tile_w - 8)
            name.align(lvgl.ALIGN.BOTTOM_MID, 0, -2)
            self.tiles.append(tile)
        self._select(self.sel)

    def _select(self, i):
        if not self.tiles:
            return
        self.sel = i % len(self.tiles)
        for j, tile in enumerate(self.tiles):
            if j == self.sel:
                tile.set_style_bg_color(_hx(theme.C_SURFACE_2), 0)
                tile.set_style_border_color(_hx(theme.C_ACCENT), 0)
                tile.set_style_border_width(theme.FOCUS_RING, 0)
            else:
                tile.set_style_bg_color(_hx(theme.C_SURFACE), 0)
                tile.set_style_border_color(_hx(theme.C_DIVIDER), 0)
                tile.set_style_border_width(theme.BORDER_HAIR, 0)

    def switch_to_foreground(self):
        super().switch_to_foreground()
        self.fr = Frame("Home", self.badge.device_name())
        self._build_tiles()
        labels = [a.name for a in self.apps[:5]]
        self.fr.make_menubar(labels)
        self.fr.replace_screen()

    def switch_to_background(self):
        self.fr = None
        self.tiles = []
        return super().switch_to_background()

    def _move(self, delta):
        self._select(self.sel + delta)

    def _launch(self, i):
        if 0 <= i < len(self.apps):
            app = self.apps[i]
            self.switch_to_background()
            app.switch_to_foreground()

    def run_foreground(self):
        kb = self.badge.keyboard
        if kb.esc():
            return  # home is the root; nothing to go back to
        key = kb.read_key()
        if key in (kb.LEFT, kb.UP):
            self._move(-1)
        elif key in (kb.RIGHT, kb.DOWN):
            self._move(1)
        elif key == kb.ENTER:
            self._launch(self.sel)
        for i, fkey in enumerate((kb.f1, kb.f2, kb.f3, kb.f4, kb.f5)):
            if fkey():
                self._launch(i)

    def run_background(self):
        # Re-show the launcher when no app is in the foreground.
        for app in self.all_apps:
            if app and app is not self and app.active_foreground:
                return
        self.switch_to_foreground()
