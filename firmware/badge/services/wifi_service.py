"""WiFi service: AP (default, SSID = device name) or STA, guarded by `network`.

Off by default (portable device). Enable in settings; AP needs no infrastructure,
STA joins a configured network. Publishes EV_WIFI_STATE. If the build lacks
`network`, status()['available'] is False and everything no-ops.
"""
try:
    import asyncio as aio
except ImportError:  # pragma: no cover
    import uasyncio as aio

try:
    import network
except ImportError:  # pragma: no cover - host or build without WiFi
    network = None

from core.services import Service
from core.events import EV_WIFI_STATE
from core.settings import Setting, TYPE_BOOL, TYPE_ENUM, TYPE_STR


def register_wifi_settings(settings):
    settings.register_many([
        Setting("wifi_enabled", TYPE_BOOL, False, "WiFi enabled", "WiFi"),
        Setting("wifi_mode", TYPE_ENUM, "ap", "WiFi mode", "WiFi", choices=["ap", "sta"]),
        Setting("wifi_ap_password", TYPE_STR, "", "AP password (>=8 or empty=open)",
                "WiFi", secret=True),
        Setting("wifi_sta_ssid", TYPE_STR, "", "Join SSID (STA)", "WiFi"),
        Setting("wifi_sta_password", TYPE_STR, "", "Join password (STA)", "WiFi", secret=True),
    ])


class WifiService(Service):
    name = "wifi"

    def __init__(self, badge):
        super().__init__(badge)
        register_wifi_settings(badge.settings)
        self.available = network is not None
        self.ap = None
        self.sta = None
        self.mode = None
        self.ip = None
        self._task = None

    def start(self):
        super().start()
        if self.available and bool(self.settings.get("wifi_enabled", False)):
            self.apply()

    def _publish(self, connected):
        self.events.publish(EV_WIFI_STATE, {"mode": self.mode, "ip": self.ip,
                                            "connected": connected,
                                            "ssid": self.badge.device_name()})

    def apply(self):
        if not self.available:
            print("wifi: `network` not in this build")
            return
        mode = self.settings.get("wifi_mode", "ap")
        if mode == "sta":
            self._start_sta()
        else:
            self._start_ap()

    def _start_ap(self):
        ssid = self.badge.device_name()
        pw = self.settings.get("wifi_ap_password", "") or ""
        ap = network.WLAN(network.AP_IF)
        ap.active(True)
        try:
            if pw and len(pw) >= 8:
                ap.config(essid=ssid, password=pw, authmode=network.AUTH_WPA2_PSK)
            else:
                ap.config(essid=ssid, authmode=network.AUTH_OPEN)
        except Exception as exc:  # noqa: BLE001
            print("wifi ap config:", exc)
        self.ap = ap
        self.mode = "ap"
        try:
            self.ip = ap.ifconfig()[0]
        except Exception:  # noqa: BLE001
            self.ip = "192.168.4.1"
        print("WiFi AP up: SSID=%s ip=%s" % (ssid, self.ip))
        self._publish(True)

    def _start_sta(self):
        ssid = self.settings.get("wifi_sta_ssid", "")
        pw = self.settings.get("wifi_sta_password", "")
        sta = network.WLAN(network.STA_IF)
        sta.active(True)
        if ssid:
            sta.connect(ssid, pw)
        self.sta = sta
        self.mode = "sta"
        if self._task is None:
            self._task = aio.create_task(self._sta_wait())

    async def _sta_wait(self):
        for _ in range(40):  # ~20s
            if self.sta and self.sta.isconnected():
                self.ip = self.sta.ifconfig()[0]
                print("WiFi STA connected ip=%s" % self.ip)
                self._publish(True)
                return
            await aio.sleep_ms(500)
        print("WiFi STA: not connected")
        self._publish(False)

    def disable(self):
        if not self.available:
            return
        for iface_id in (getattr(network, "AP_IF", 1), getattr(network, "STA_IF", 0)):
            try:
                network.WLAN(iface_id).active(False)
            except Exception:  # noqa: BLE001
                pass
        self.ip = None
        self.mode = None
        self._publish(False)

    def status(self):
        return {"name": self.name, "available": self.available,
                "mode": self.mode, "ip": self.ip}
