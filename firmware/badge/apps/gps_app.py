"""Live GPS position (Frame UI). F1 shares the current position over the mesh."""
import lvgl

from apps.base_app import BaseApp
from ui.frame import Frame
from ui import theme
from core.manifest import AppManifest

APP_NAME = "GPS"
_hx = lvgl.color_hex


class App(BaseApp):
    MANIFEST = AppManifest("GPS", requires=("gps",), description="Live GPS position")

    def __init__(self, name, badge):
        super().__init__(name, badge)
        self.foreground_sleep_ms = 300
        self.fr = None
        self.label = None

    def switch_to_foreground(self):
        super().switch_to_foreground()
        self.fr = Frame("GPS", "")
        self.label = lvgl.label(self.fr.body)
        self.label.set_style_text_font(theme.f_body(), 0)
        self.label.set_style_text_color(_hx(theme.C_TEXT), 0)
        self.label.set_width(theme.CONTENT_W - 2 * theme.PAD_M)
        self.label.set_text("Starting GPS...")
        self.fr.make_menubar(["Share", "", "", "", "Back"])
        self.fr.replace_screen()

    def switch_to_background(self):
        self.fr = None
        self.label = None
        return super().switch_to_background()

    def run_foreground(self):
        kb = self.badge.keyboard
        if kb.esc() or kb.f5():
            self.switch_to_background()
            return
        gps = self.badge.services.get("gps")
        if kb.f1():
            self._share(gps)
        if self.label is None:
            return
        if gps is None:
            self.label.set_text("GPS service unavailable")
            return
        if not gps.status().get("enabled"):
            self.label.set_text("GPS is disabled.\nEnable it in Settings > GPS,\n"
                                "and wire the ATGM336H to J6 (TX -> IO12).")
            self.fr.set_context("off")
            return
        fix = gps.fix()
        if not fix:
            self.label.set_text("Acquiring fix...\n(go outdoors)")
            self.fr.set_context("search")
            return
        self.fr.set_context(("3D" if fix.get("sats", 0) >= 4 else "2D") + " %dsat" % fix.get("sats", 0))
        self.label.set_text(self._format(fix))

    def _format(self, fix):
        track = fix.get("track")
        track_s = ("%.0f deg" % track) if track is not None else "-"
        return ("Lat  %.6f\nLon  %.6f\nAlt  %d m\nSpeed %.1f kn   Course %s"
                % (fix["lat"], fix["lon"], fix.get("alt", 0), fix.get("speed", 0.0), track_s))

    def _share(self, gps):
        router = getattr(self.badge, "net_router", None)
        if gps and gps.fix() and router:
            f = gps.fix()
            router.send_position(f["lat"], f["lon"], f.get("alt", 0), f.get("ts", 0))
            self.fr.set_context("shared")
