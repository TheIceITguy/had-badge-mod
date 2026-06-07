"""Minimal ``network`` (WiFi) shim for host tests."""

STA_IF = 0
AP_IF = 1


class WLAN:
    def __init__(self, iface=STA_IF):
        self.iface = iface
        self._active = False
        self._connected = False

    def active(self, value=None):
        if value is None:
            return self._active
        self._active = bool(value)
        return None

    def config(self, *a, **k):
        return None

    def connect(self, *a, **k):
        self._connected = True

    def disconnect(self):
        self._connected = False

    def isconnected(self):
        return self._connected

    def ifconfig(self, *a):
        return ("0.0.0.0", "0.0.0.0", "0.0.0.0", "0.0.0.0")
