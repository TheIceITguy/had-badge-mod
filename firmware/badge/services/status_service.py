"""Owns the persistent left status sidebar on lvgl.layer_top().

Built once at boot; survives every screen swap. Content screens are inset by
theme.SIDEBAR_W (via ui/frame.py), so the opaque sidebar floats over an empty
strip and never overlaps app content. Subscribes to system events and updates the
battery/wifi/mesh/gps icons (cheap recolor only — safe from the event stack).
"""
import lvgl

from core.services import Service
from core.events import (EV_BATTERY, EV_WIFI_STATE, EV_MESH_NODE_UPDATE,
                         EV_BACKEND_CHANGED, EV_POSITION_UPDATE,
                         EV_MESSAGE_RECEIVED, EV_MESSAGES_READ)
from ui import icons, theme


class StatusService(Service):
    name = "status"

    def __init__(self, badge):
        super().__init__(badge)
        self.bar = None
        self._up = False
        self._gps_enabled = None

    def start(self):
        super().start()
        theme.init_styles()
        self._build_sidebar()
        # Reflect current backend immediately.
        router = self.badge.services.get("net")
        if router is not None:
            self._up = bool(router.active_name) and router.active_name != "none"
        self.mesh.set_state(self._up, self._peer_count())
        self.events.subscribe(EV_BATTERY, self._on_battery)
        self.events.subscribe(EV_WIFI_STATE, self._on_wifi)
        self.events.subscribe(EV_MESH_NODE_UPDATE, self._on_mesh)
        self.events.subscribe(EV_BACKEND_CHANGED, self._on_backend)
        self.events.subscribe(EV_POSITION_UPDATE, self._on_pos)
        self.events.subscribe(EV_MESSAGE_RECEIVED, self._on_msg)
        self.events.subscribe(EV_MESSAGES_READ, self._on_read)

    def _build_sidebar(self):
        top = lvgl.layer_top()
        bar = lvgl.obj(top)
        bar.set_pos(0, 0)
        bar.set_size(theme.SIDEBAR_W, theme.SCREEN_H)
        bar.set_scrollbar_mode(0)
        bar.add_style(theme.st_sidebar, 0)
        # right hairline divider
        div = lvgl.obj(bar)
        div.set_pos(theme.SIDEBAR_W - 1, 0)
        div.set_size(1, theme.SCREEN_H)
        div.set_style_border_width(0, 0)
        div.set_style_radius(0, 0)
        div.set_style_bg_color(lvgl.color_hex(theme.C_DIVIDER), 0)
        self.bar = bar

        self.mesh = icons.MeshIcon(bar); self.mesh.o.set_pos(5, 8)
        self.wifi = icons.WifiIcon(bar); self.wifi.o.set_pos(5, 30)
        self.gps = icons.GpsIcon(bar); self.gps.o.set_pos(7, 52)
        self.batt = icons.BatteryIcon(bar); self.batt.o.set_pos(3, 120)

    def _peer_count(self):
        mesh = self.badge.services.get("mesh")
        try:
            return mesh.count() if mesh else 0
        except Exception:  # noqa: BLE001
            return 0

    # --- event handlers (fast, no await, recolor only) ------------------
    def _on_battery(self, p):
        self.batt.set_state(p.get("pct"), bool(p.get("charging")),
                            bool(p.get("usb")), bool(p.get("present")))

    def _on_wifi(self, p):
        mode = p.get("mode")
        if mode == "ap":
            state = "ap"
        elif p.get("connected"):
            state = "conn"
        else:
            state = "off"
        self.wifi.set_state(state, p.get("rssi"))

    def _on_mesh(self, _rec):
        self.mesh.set_state(self._up, self._peer_count(), tx=False)

    def _on_backend(self, name):
        self._up = bool(name) and name != "none"
        self.mesh.set_state(self._up, self._peer_count())

    def _on_pos(self, p):
        self.gps.set_state(theme.gps_state(True, bool(p.get("lat")), p.get("sats")))

    def _on_msg(self, _m):
        self.mesh.notify(True)      # unread indicator until Messages is opened

    def _on_read(self, _x):
        self.mesh.notify(False)
