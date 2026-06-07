"""Core settings schema + device-identity helpers.

Registers the always-present settings (device name, and the stock keys the legacy
firmware already uses) so the settings app and WebUI can render them. Feature
milestones (radio/region, WiFi, GPS, power) register their own settings from the
services that own them.
"""
import binascii

try:
    import machine
except ImportError:  # host tests
    machine = None

from core.settings import Setting, TYPE_STR, TYPE_INT


def _uid_hex():
    if machine is None:
        return "00000000000000"
    return binascii.hexlify(machine.unique_id()).decode().upper()


def node_id():
    """The 32-bit node id derived from the ESP32 unique id (Meshtastic NodeNum)."""
    if machine is None:
        return 0
    return int.from_bytes(machine.unique_id()[2:6], "big")


def default_device_name():
    return "Badge-" + _uid_hex()[-4:]


def default_short_name():
    return _uid_hex()[-4:]


def register_core_settings(settings):
    settings.register_many([
        Setting("device_name", TYPE_STR, default_device_name(),
                "Device name", "Device",
                validate=lambda s: 0 < len(s) <= 32,
                help="Used as the Meshtastic long name, WiFi AP SSID, and chat alias."),
        Setting("short_name", TYPE_STR, default_short_name(),
                "Short name", "Device",
                validate=lambda s: 0 < len(s) <= 4,
                help="Up to 4 characters; shown on the Meshtastic node list."),
        # Stock keys surfaced in the schema (back-compatible with existing values).
        Setting("alias", TYPE_STR, "", "Chat alias", "Device"),
        Setting("radio_tx_power", TYPE_INT, 9, "TX power (dBm)", "Radio",
                minv=-9, maxv=22),
        Setting("chat_ttl", TYPE_INT, 3, "Mesh hop limit", "Radio", minv=0, maxv=15),
    ])
