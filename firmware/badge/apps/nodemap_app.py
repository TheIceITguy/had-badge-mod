"""Mesh node list (Frame UI): heard nodes with distance/bearing (if we have a
fix), SNR, and how long ago they were last heard."""
import time

import lvgl

from apps.base_app import BaseApp
from ui.frame import Frame
from ui import theme
from core.events import EV_MESH_NODE_UPDATE
from core.manifest import AppManifest
from services.geo import distance_m, bearing_deg

APP_NAME = "NodeMap"
_DIRS = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
_hx = lvgl.color_hex


class App(BaseApp):
    MANIFEST = AppManifest("NodeMap", requires=("mesh",), description="Mesh node list/map")

    def __init__(self, name, badge):
        super().__init__(name, badge)
        self.foreground_sleep_ms = 400
        self.fr = None
        self.label = None
        self.dirty = True

    def start(self):
        super().start()
        self.badge.events.subscribe(EV_MESH_NODE_UPDATE, self._on_update)

    def _on_update(self, _rec):
        self.dirty = True

    def _my_fix(self):
        gps = self.badge.services.get("gps")
        fix = gps.fix() if gps else None
        return fix if (fix and fix.get("valid")) else None

    def _text(self):
        mesh = self.badge.services.get("mesh")
        if mesh is None:
            return "mesh backend not active"
        nodes = sorted(mesh.nodes(), key=lambda n: n.last_heard, reverse=True)
        if not nodes:
            return "no nodes heard yet"
        my = self._my_fix()
        now = int(time.time())
        lines = []
        for n in nodes[:12]:
            age = now - n.last_heard if n.last_heard else 0
            info = "%ds" % age
            if n.snr is not None:
                info += " snr%.0f" % n.snr
            if my and n.has_position():
                d = distance_m(my["lat"], my["lon"], n.lat, n.lon)
                b = bearing_deg(my["lat"], my["lon"], n.lat, n.lon)
                info = "%.1fkm %s  " % (d / 1000.0, _DIRS[int((b + 22.5) // 45) % 8]) + info
            lines.append("%-10s %s" % (n.name()[:10], info))
        return "\n".join(lines)

    def switch_to_foreground(self):
        super().switch_to_foreground()
        self.fr = Frame("Nodes", "")
        self.label = lvgl.label(self.fr.body)
        self.label.set_style_text_font(theme.f_body(), 0)
        self.label.set_style_text_color(_hx(theme.C_TEXT), 0)
        self.label.set_width(theme.CONTENT_W - 2 * theme.PAD_M)
        self.fr.make_menubar(["Refresh", "", "", "", "Back"])
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
        mesh = self.badge.services.get("mesh")
        self.fr.set_context("%d nodes" % (mesh.count() if mesh else 0))
        self.label.set_text(self._text())
        self.dirty = False

    def run_foreground(self):
        kb = self.badge.keyboard
        if kb.esc() or kb.f5():
            self.switch_to_background()
            return
        if kb.f1() or self.dirty:
            self._render()
