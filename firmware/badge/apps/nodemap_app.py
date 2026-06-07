"""Mesh node list/map: shows heard nodes with distance/bearing (if we have a fix),
SNR, and how long ago they were last heard."""
import time

from apps.base_app import BaseApp
from ui.chat import Chat
from core.events import EV_MESH_NODE_UPDATE
from core.manifest import AppManifest
from services.geo import distance_m, bearing_deg

APP_NAME = "NodeMap"
_DIRS = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]


class App(BaseApp):
    MANIFEST = AppManifest("NodeMap", requires=("mesh",), description="Mesh node list/map")

    def __init__(self, name, badge):
        super().__init__(name, badge)
        self.foreground_sleep_ms = 400
        self.page = None
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

    def _rows(self):
        mesh = self.badge.services.get("mesh")
        if mesh is None:
            return [("", "mesh backend not active")]
        nodes = sorted(mesh.nodes(), key=lambda n: n.last_heard, reverse=True)
        if not nodes:
            return [("", "no nodes heard yet")]
        my = self._my_fix()
        now = int(time.time())
        rows = []
        for n in nodes:
            age = now - n.last_heard if n.last_heard else 0
            info = "%ds ago" % age
            if n.snr is not None:
                info += "  snr %.0f" % n.snr
            if my and n.has_position():
                d = distance_m(my["lat"], my["lon"], n.lat, n.lon)
                b = bearing_deg(my["lat"], my["lon"], n.lat, n.lon)
                card = _DIRS[int((b + 22.5) // 45) % 8]
                info = ("%.2fkm %s  " % (d / 1000.0, card)) + info
            rows.append((n.name()[:10], info))
        return rows

    def switch_to_foreground(self):
        super().switch_to_foreground()
        self.page = Chat(
            infobar_contents=("Nodes", self.badge.device_name()),
            menubar_labels=("Refresh", "", "", "", "Home"),
            messages=[],
        )
        self.page.add_message_rows(1, left_width=80)
        self.dirty = True
        self._render()
        self.page.replace_screen()

    def switch_to_background(self):
        self.page = None
        return super().switch_to_background()

    def _render(self):
        if self.page is not None:
            self.page.populate_message_rows(self._rows())
            self.dirty = False

    def run_foreground(self):
        if self.badge.keyboard.f5():
            self.switch_to_background()
            return
        if self.badge.keyboard.f1() or self.dirty:
            self._render()
