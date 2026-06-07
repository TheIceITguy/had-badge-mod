"""GPS service for the ATGM336H (NMEA over UART on header J6 / IO11-IO12).

Reads NMEA, maintains the latest fix, and publishes EV_POSITION_UPDATE (our own
position) and EV_TIME_SYNC (for the time service). Off by default; enable in
settings (a reboot, or calling enable(), opens the UART). Only RX is required to
read position; TX is wired for optional module configuration.
"""
try:
    import asyncio as aio
except ImportError:  # pragma: no cover
    import uasyncio as aio

from machine import Pin, UART

from hardware import board
from core.services import Service
from core.events import EV_POSITION_UPDATE, EV_TIME_SYNC
from core.settings import Setting, TYPE_BOOL, TYPE_INT
from services.nmea import NmeaParser


def register_gps_settings(settings):
    settings.register_many([
        Setting("gps_enabled", TYPE_BOOL, False, "GPS enabled", "GPS",
                help="Enable after wiring the ATGM336H to J6 (GPS TX -> IO12)."),
        Setting("gps_uart_id", TYPE_INT, 1, "GPS UART id", "GPS", minv=0, maxv=2),
        Setting("gps_rx_pin", TYPE_INT, board.GPS_RX_PIN, "GPS RX pin (ESP32 in)", "GPS"),
        Setting("gps_tx_pin", TYPE_INT, board.GPS_TX_PIN, "GPS TX pin (ESP32 out)", "GPS"),
        Setting("gps_baud", TYPE_INT, 9600, "GPS baud", "GPS"),
    ])


class GpsService(Service):
    name = "gps"

    def __init__(self, badge):
        super().__init__(badge)
        register_gps_settings(badge.settings)
        self.uart = None
        self.parser = NmeaParser()
        self._fix = None
        self._task = None
        self._time_set = False
        self._rmc = {}
        self._gga = {}

    def start(self):
        super().start()
        if self._enabled():
            self._open()

    def _enabled(self):
        return bool(self.settings.get("gps_enabled", False)) if self.settings else False

    def _open(self):
        try:
            uart_id = int(self.settings.get("gps_uart_id", 1))
            rx = int(self.settings.get("gps_rx_pin", board.GPS_RX_PIN))
            tx = int(self.settings.get("gps_tx_pin", board.GPS_TX_PIN))
            baud = int(self.settings.get("gps_baud", 9600))
            self.uart = UART(uart_id, baudrate=baud, tx=Pin(tx), rx=Pin(rx), timeout=50)
            if self._task is None:
                self._task = aio.create_task(self._read_loop())
            print("GPS: UART%d rx=%d tx=%d @%d baud" % (uart_id, rx, tx, baud))
        except Exception as exc:  # noqa: BLE001
            print("GPS: failed to open UART:", exc)
            self.uart = None

    def enable(self):
        self.settings.set("gps_enabled", True)
        if self.uart is None:
            self._open()

    def disable(self):
        self.settings.set("gps_enabled", False)

    def has_fix(self):
        return bool(self._fix and self._fix.get("valid"))

    def fix(self):
        return self._fix

    def status(self):
        return {"name": self.name, "enabled": self._enabled(), "fix": self.has_fix()}

    def _merge(self):
        rmc, gga = self._rmc, self._gga
        lat = rmc.get("lat") if rmc.get("lat") is not None else gga.get("lat")
        lon = rmc.get("lon") if rmc.get("lon") is not None else gga.get("lon")
        if lat is None or lon is None:
            return None
        return {
            "lat": lat, "lon": lon, "alt": gga.get("alt", 0), "ts": rmc.get("ts", 0),
            "sats": gga.get("sats", 0), "speed": rmc.get("speed", 0.0),
            "track": rmc.get("track"), "valid": bool(rmc.get("valid") or gga.get("valid")),
            "source": "gps",
        }

    async def _read_loop(self):
        while True:
            if not self._enabled() or self.uart is None:
                await aio.sleep_ms(500)
                continue
            try:
                data = self.uart.read()
                if data:
                    for s in self.parser.feed(data):
                        if s["type"] == "RMC":
                            self._rmc = s
                            if s.get("datetime") and not self._time_set:
                                self.events.publish(EV_TIME_SYNC, s["datetime"])
                                self._time_set = True
                        elif s["type"] == "GGA":
                            self._gga = s
                    fix = self._merge()
                    if fix and fix["valid"]:
                        self._fix = fix
                        self.events.publish(EV_POSITION_UPDATE, fix)
            except Exception as exc:  # noqa: BLE001
                print("GPS read error:", exc)
            await aio.sleep_ms(200)
