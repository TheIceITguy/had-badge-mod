"""Heading display (Frame UI). No magnetometer, so heading is GPS course over
ground and is only meaningful while moving."""
import lvgl

from apps.base_app import BaseApp
from ui.frame import Frame
from ui import theme
from core.manifest import AppManifest

APP_NAME = "Compass"
_DIRS = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
_hx = lvgl.color_hex


class App(BaseApp):
    MANIFEST = AppManifest("Compass", requires=("gps",),
                           description="Heading (GPS course over ground)")

    def __init__(self, name, badge):
        super().__init__(name, badge)
        self.foreground_sleep_ms = 300
        self.fr = None
        self.big = None
        self.sub = None

    def switch_to_foreground(self):
        super().switch_to_foreground()
        self.fr = Frame("Compass", "")
        self.big = lvgl.label(self.fr.body)
        self.big.set_style_text_font(theme.f_hero(), 0)
        self.big.set_style_text_color(_hx(theme.C_ACCENT), 0)
        self.big.align(lvgl.ALIGN.TOP_MID, 0, 6)
        self.big.set_text("--")
        self.sub = lvgl.label(self.fr.body)
        self.sub.set_style_text_font(theme.f_body(), 0)
        self.sub.set_style_text_color(_hx(theme.C_TEXT_DIM), 0)
        self.sub.align(lvgl.ALIGN.TOP_MID, 0, 44)
        self.sub.set_text("waiting for GPS")
        self.fr.make_menubar(["", "", "", "", "Back"])
        self.fr.replace_screen()

    def switch_to_background(self):
        self.fr = None
        self.big = None
        self.sub = None
        return super().switch_to_background()

    @staticmethod
    def _cardinal(deg):
        return _DIRS[int((deg + 22.5) // 45) % 8]

    def run_foreground(self):
        kb = self.badge.keyboard
        if kb.esc() or kb.f5():
            self.switch_to_background()
            return
        if self.big is None:
            return
        gps = self.badge.services.get("gps")
        if gps is None or not gps.status().get("enabled"):
            self.big.set_text("--")
            self.sub.set_text("GPS disabled (Settings > GPS)")
            return
        fix = gps.fix()
        track = fix.get("track") if fix else None
        if track is None:
            self.big.set_text("--")
            self.sub.set_text("move to get a course (no compass IC)")
        else:
            self.big.set_text("%.0f %s" % (track, self._cardinal(track)))
            self.sub.set_text("speed %.1f kn" % (fix.get("speed", 0.0)))
