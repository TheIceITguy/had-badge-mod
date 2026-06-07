"""Heading display. Note: the badge has no magnetometer, so heading is the GPS
course-over-ground and is only meaningful while moving."""
import lvgl

from apps.base_app import BaseApp
from ui.page import Page
from core.manifest import AppManifest

APP_NAME = "Compass"
_DIRS = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]


class App(BaseApp):
    MANIFEST = AppManifest("Compass", requires=("gps",),
                           description="Heading (GPS course over ground)")

    def __init__(self, name, badge):
        super().__init__(name, badge)
        self.foreground_sleep_ms = 300
        self.page = None
        self.label = None

    def switch_to_foreground(self):
        super().switch_to_foreground()
        self.page = Page()
        self.page.create_infobar(("Compass", self.badge.device_name()))
        self.page.create_content()
        self.label = lvgl.label(self.page.content)
        self.label.set_style_text_font(lvgl.font_montserrat_16, 0)
        self.label.set_text("Waiting for GPS...")
        self.page.create_menubar(("", "", "", "", "Home"))
        self.page.replace_screen()

    def switch_to_background(self):
        self.page = None
        self.label = None
        return super().switch_to_background()

    @staticmethod
    def _cardinal(deg):
        return _DIRS[int((deg + 22.5) // 45) % 8]

    def run_foreground(self):
        if self.badge.keyboard.f5():
            self.switch_to_background()
            return
        if self.label is None:
            return
        gps = self.badge.services.get("gps")
        if gps is None or not gps.status().get("enabled"):
            self.label.set_text("GPS disabled (Config > GPS)")
            return
        fix = gps.fix()
        track = fix.get("track") if fix else None
        if track is None:
            self.label.set_text("No heading.\nMove to get a course\nover ground (no compass IC).")
        else:
            self.label.set_text("Heading: %.0f deg  %s\nSpeed: %.1f kn"
                                % (track, self._cardinal(track), fix.get("speed", 0.0)))
