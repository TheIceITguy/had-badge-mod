"""Live GPS position viewer. F1 shares the current position over the mesh."""
import lvgl

from apps.base_app import BaseApp
from ui.page import Page
from core.manifest import AppManifest

APP_NAME = "GPS"


class App(BaseApp):
    MANIFEST = AppManifest("GPS", requires=("gps",), description="Live GPS position")

    def __init__(self, name, badge):
        super().__init__(name, badge)
        self.foreground_sleep_ms = 300
        self.page = None
        self.label = None

    def switch_to_foreground(self):
        super().switch_to_foreground()
        self.page = Page()
        self.page.create_infobar(("GPS", self.badge.device_name()))
        self.page.create_content()
        self.label = lvgl.label(self.page.content)
        self.label.set_style_text_font(lvgl.font_montserrat_16, 0)
        self.label.set_text("Starting GPS...")
        self.page.create_menubar(("Share", "", "", "", "Home"))
        self.page.replace_screen()

    def switch_to_background(self):
        self.page = None
        self.label = None
        return super().switch_to_background()

    def run_foreground(self):
        if self.badge.keyboard.f5():
            self.switch_to_background()
            return
        gps = self.badge.services.get("gps")
        if self.badge.keyboard.f1():
            self._share(gps)
        if self.label is None:
            return
        if gps is None:
            self.label.set_text("GPS service unavailable")
            return
        if not gps.status().get("enabled"):
            self.label.set_text("GPS disabled.\nEnable in Config > GPS,\nwire ATGM336H to J6 (TX->IO12).")
            return
        fix = gps.fix()
        if not fix:
            self.label.set_text("Acquiring fix...\n(go outdoors)")
            return
        self.label.set_text(self._format(fix))

    def _format(self, fix):
        track = fix.get("track")
        track_s = ("%.0f deg" % track) if track is not None else "-"
        return ("Lat: %.6f\nLon: %.6f\nAlt: %d m    Sats: %d\nSpeed: %.1f kn   Course: %s\n%s"
                % (fix["lat"], fix["lon"], fix.get("alt", 0), fix.get("sats", 0),
                   fix.get("speed", 0.0), track_s, "FIX" if fix.get("valid") else "no fix"))

    def _share(self, gps):
        router = getattr(self.badge, "net_router", None)
        if gps and gps.fix() and router:
            f = gps.fix()
            router.send_position(f["lat"], f["lon"], f.get("alt", 0), f.get("ts", 0))
            if self.page:
                self.page.infobar_right.set_text("position shared")
