"""Host tests for the schema-driven SettingsRegistry."""
import pytest

from core.settings import SettingsRegistry, Setting, TYPE_INT, TYPE_BOOL, TYPE_ENUM, TYPE_STR


class FakeConfig:
    def __init__(self, init=None):
        self.d = dict(init or {})
        self.flushed = 0

    def get(self, key, default=None):
        return self.d.get(key, default)

    def set(self, key, value):
        self.d[key] = value

    def flush(self):
        self.flushed += 1


def make():
    cfg = FakeConfig()
    reg = SettingsRegistry(cfg)
    reg.register_many([
        Setting("device_name", TYPE_STR, "Badge-0000", "Device name", "Device"),
        Setting("tx_power", TYPE_INT, 9, "TX power", "Radio", minv=0, maxv=22),
        Setting("wifi_on", TYPE_BOOL, False, "WiFi", "WiFi"),
        Setting("region", TYPE_ENUM, "EU_868", "Region", "Radio",
                choices=["EU_868", "US"]),
        Setting("wifi_pw", TYPE_STR, "", "WiFi password", "WiFi", secret=True),
    ])
    return cfg, reg


def test_defaults_when_unset():
    _, reg = make()
    assert reg.get("device_name") == "Badge-0000"
    assert reg.get("tx_power") == 9
    assert reg.get("wifi_on") is False
    assert reg.get("region") == "EU_868"


def test_set_get_typed_and_persist():
    cfg, reg = make()
    reg.set("tx_power", 17)
    assert reg.get("tx_power") == 17
    assert cfg.d["tx_power"] == "17"      # stored as string
    assert cfg.flushed >= 1
    reg.set("wifi_on", True)
    assert reg.get("wifi_on") is True
    assert cfg.d["wifi_on"] == "true"


def test_int_range_validation():
    _, reg = make()
    with pytest.raises(ValueError):
        reg.set("tx_power", 99)
    with pytest.raises(ValueError):
        reg.set("tx_power", "notnum")


def test_enum_validation():
    _, reg = make()
    reg.set("region", "US")
    assert reg.get("region") == "US"
    with pytest.raises(ValueError):
        reg.set("region", "MARS")


def test_backcompat_bytes_values():
    # Stock firmware writes bytes like b'9' and b'false'.
    cfg = FakeConfig({"tx_power": b"9", "wifi_on": b"false"})
    reg = SettingsRegistry(cfg)
    reg.register_many([
        Setting("tx_power", TYPE_INT, 0, group="Radio"),
        Setting("wifi_on", TYPE_BOOL, True, group="WiFi"),
    ])
    assert reg.get("tx_power") == 9
    assert reg.get("wifi_on") is False


def test_groups_and_as_dict_secret_masked():
    _, reg = make()
    assert reg.groups() == ["Device", "Radio", "WiFi"]
    reg.set("wifi_pw", "hunter2")
    d = reg.as_dict()
    assert d["wifi_pw"]["secret"] is True
    assert d["wifi_pw"]["value"] == "••••"   # not the real password
    assert d["region"]["choices"] == ["EU_868", "US"]


def test_update_from_dict_collects_errors():
    _, reg = make()
    errors = reg.update_from_dict({"tx_power": 5, "region": "MARS", "unknown": 1})
    assert reg.get("tx_power") == 5
    assert any("region" in e for e in errors)
