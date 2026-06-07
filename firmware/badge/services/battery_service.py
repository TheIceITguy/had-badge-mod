"""Battery + power service.

- Reads battery voltage from an ADC pin *if configured* (the stock board has no
  confirmed battery-sense divider, so this is off by default: set bat_adc_pin and
  bat_divider once you've confirmed the wiring). Publishes EV_BATTERY /
  EV_BATTERY_LOW.
- Draws a persistent battery % indicator on the LVGL top layer, so it shows on
  EVERY screen (home, apps, settings) and survives screen swaps. Shows 'n/a' when
  no sense line is configured.
- Dims the backlight after inactivity (reset on any key), to save power.
"""
try:
    import asyncio as aio
except ImportError:  # pragma: no cover
    import uasyncio as aio

import lvgl

from machine import Pin, ADC

from core.services import Service
from core.events import EV_BATTERY, EV_BATTERY_LOW
from core.settings import Setting, TYPE_INT, TYPE_STR

# LiPo voltage -> percent reference points (empty .. full).
_V_EMPTY = 3.30
_V_FULL = 4.20


def volts_to_pct(v):
    if v is None:
        return None
    pct = (v - _V_EMPTY) / (_V_FULL - _V_EMPTY) * 100.0
    return int(max(0, min(100, round(pct))))


def register_battery_settings(settings):
    settings.register_many([
        Setting("bat_adc_pin", TYPE_INT, -1, "Battery ADC pin (-1=off)", "Power",
                minv=-1, maxv=48,
                help="GPIO of the battery-sense divider. Leave -1 until confirmed."),
        Setting("bat_divider", TYPE_STR, "2.0", "Battery divider ratio", "Power"),
        Setting("bat_low_threshold_pct", TYPE_INT, 20, "Low-battery warning %", "Power",
                minv=1, maxv=99),
        Setting("bat_poll_s", TYPE_INT, 30, "Battery poll interval (s)", "Power",
                minv=5, maxv=600),
        Setting("backlight_timeout_s", TYPE_INT, 60, "Backlight dim timeout (s, 0=off)",
                "Power", minv=0, maxv=3600),
        Setting("backlight_bright", TYPE_INT, 500, "Backlight bright duty", "Power",
                minv=10, maxv=1023),
        Setting("backlight_dim", TYPE_INT, 40, "Backlight dim duty", "Power",
                minv=0, maxv=1023),
    ])


class BatteryService(Service):
    name = "battery"

    def __init__(self, badge):
        super().__init__(badge)
        register_battery_settings(badge.settings)
        self.adc = None
        self.available = False
        self.volts = None
        self.pct = None
        self._overlay = None
        self._task = None
        self._dim_task = None

    # --- provider -------------------------------------------------------
    def _setup_adc(self):
        try:
            pin = int(self.settings.get("bat_adc_pin", -1))
        except (TypeError, ValueError):
            pin = -1
        if pin < 0:
            self.available = False
            return
        try:
            self.adc = ADC(Pin(pin))
            try:
                self.adc.atten(ADC.ATTN_11DB)  # full ~0-3.3V range
            except Exception:  # noqa: BLE001
                pass
            self.available = True
        except Exception as exc:  # noqa: BLE001
            print("battery: ADC setup failed:", exc)
            self.available = False

    def _read_volts(self):
        if not self.available or self.adc is None:
            return None
        try:
            divider = float(self.settings.get("bat_divider", "2.0"))
        except (TypeError, ValueError):
            divider = 2.0
        try:
            raw = self.adc.read_u16()
        except Exception:  # noqa: BLE001
            return None
        return raw / 65535.0 * 3.3 * divider

    # --- lifecycle ------------------------------------------------------
    def start(self):
        super().start()
        self._setup_adc()
        self._make_overlay()
        self._task = aio.create_task(self._poll_loop())
        self._dim_task = aio.create_task(self._backlight_loop())

    def status(self):
        return {"name": self.name, "available": self.available,
                "pct": self.pct, "volts": self.volts}

    # --- overlay (top layer = every screen) -----------------------------
    def _make_overlay(self):
        try:
            top = lvgl.layer_top()
            self._overlay = lvgl.label(top)
            self._overlay.set_style_text_font(lvgl.font_montserrat_12, 0)
            self._overlay.align(lvgl.ALIGN.TOP_RIGHT, -2, 1)
            self._overlay.set_text("--")
        except Exception as exc:  # noqa: BLE001
            print("battery: overlay create failed:", exc)
            self._overlay = None

    def _update_overlay(self):
        if self._overlay is None:
            return
        try:
            self._overlay.set_text("n/a" if self.pct is None else ("%d%%" % self.pct))
        except Exception:  # noqa: BLE001
            pass

    # --- loops ----------------------------------------------------------
    async def _poll_loop(self):
        while True:
            self.volts = self._read_volts()
            self.pct = volts_to_pct(self.volts)
            self._update_overlay()
            if self.pct is not None:
                self.events.publish(EV_BATTERY, {"volts": self.volts, "pct": self.pct,
                                                 "charging": None})
                try:
                    low = int(self.settings.get("bat_low_threshold_pct", 20))
                except (TypeError, ValueError):
                    low = 20
                if self.pct <= low:
                    self.events.publish(EV_BATTERY_LOW, {"pct": self.pct})
            try:
                interval = int(self.settings.get("bat_poll_s", 30))
            except (TypeError, ValueError):
                interval = 30
            await aio.sleep(interval)

    async def _backlight_loop(self):
        last_activity_seq = -1
        idle = 0
        dimmed = False
        bl = getattr(self.badge.display, "backlight", None)
        while True:
            try:
                timeout = int(self.settings.get("backlight_timeout_s", 60))
                bright = int(self.settings.get("backlight_bright", 500))
                dim = int(self.settings.get("backlight_dim", 40))
            except (TypeError, ValueError):
                timeout, bright, dim = 60, 500, 40
            seq = getattr(self.badge.keyboard, "activity_count", 0)
            if seq != last_activity_seq:
                last_activity_seq = seq
                idle = 0
                if dimmed and bl is not None:
                    bl.duty(bright)
                    dimmed = False
            else:
                idle += 1
            if timeout and not dimmed and idle >= timeout and bl is not None:
                bl.duty(dim)
                dimmed = True
            await aio.sleep(1)
