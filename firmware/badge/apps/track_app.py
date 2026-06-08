"""GPS breadcrumb logger (Frame UI). F1 starts/stops recording to a CSV."""
import time

import lvgl

from apps.base_app import BaseApp
from ui.frame import Frame
from ui import theme
from core.events import EV_POSITION_UPDATE
from core.manifest import AppManifest

APP_NAME = "Track"
_hx = lvgl.color_hex


class App(BaseApp):
    MANIFEST = AppManifest("Track", requires=("gps",), description="GPS breadcrumb logger")

    def __init__(self, name, badge):
        super().__init__(name, badge)
        self.foreground_sleep_ms = 300
        self.fr = None
        self.label = None
        self.recording = False
        self.count = 0
        self.fh = None
        self.path = None
        self.dirty = True

    def start(self):
        super().start()
        self.badge.events.subscribe(EV_POSITION_UPDATE, self._on_pos)

    def _on_pos(self, fix):
        if not (self.recording and self.fh and fix and fix.get("valid")):
            return
        try:
            self.fh.write("%d,%.6f,%.6f,%d,%d\n" % (
                fix.get("ts", 0), fix["lat"], fix["lon"], fix.get("alt", 0), fix.get("sats", 0)))
            self.fh.flush()
            self.count += 1
            self.dirty = True
        except Exception as exc:  # noqa: BLE001
            print("track write error:", exc)

    def _start_rec(self):
        try:
            self.path = "/data/track_%d.csv" % int(time.time())
            self.fh = open(self.path, "w")
            self.fh.write("ts,lat,lon,alt,sats\n")
            self.fh.flush()
            self.count = 0
            self.recording = True
        except Exception as exc:  # noqa: BLE001
            print("track open error:", exc)

    def _stop_rec(self):
        self.recording = False
        if self.fh:
            try:
                self.fh.close()
            except Exception:  # noqa: BLE001
                pass
            self.fh = None

    def switch_to_foreground(self):
        super().switch_to_foreground()
        self.fr = Frame("Track", "")
        self.label = lvgl.label(self.fr.body)
        self.label.set_style_text_font(theme.f_body(), 0)
        self.label.set_style_text_color(_hx(theme.C_TEXT), 0)
        self.label.set_width(theme.CONTENT_W - 2 * theme.PAD_M)
        self.fr.make_menubar(["Rec/Stop", "", "", "", "Back"])
        self.dirty = True
        self._render()
        self.fr.replace_screen()

    def switch_to_background(self):
        self.fr = None
        self.label = None
        return super().switch_to_background()

    def _render(self):
        if self.label is None:
            return
        if self.recording:
            self.fr.set_context("REC")
            self.label.set_text("Recording -> %s\npoints: %d\n\nF1 to stop" % (self.path, self.count))
        else:
            self.fr.set_context("idle")
            self.label.set_text("Breadcrumb logger\npoints: %d\n\nF1 to start recording" % self.count)
        self.dirty = False

    def run_foreground(self):
        kb = self.badge.keyboard
        if kb.esc() or kb.f5():
            self.switch_to_background()
            return
        if kb.f1():
            self._stop_rec() if self.recording else self._start_rec()
            self.dirty = True
        if self.dirty:
            self._render()
