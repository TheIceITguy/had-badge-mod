"""Host tests for the pure theme state->visual mappers."""
from ui import theme


def test_battery_state():
    assert theme.battery_state(None, False, False, False) == "none"
    assert theme.battery_state(None, False, True, False) == "none"   # not present
    assert theme.battery_state(50, False, True, True) == "ok"
    assert theme.battery_state(100, False, True, True) == "usb_full"
    assert theme.battery_state(10, False, False, True) == "crit"
    assert theme.battery_state(30, False, False, True) == "low"
    assert theme.battery_state(80, False, False, True) == "ok"
    assert theme.battery_state(50, True, False, True) == "charging"


def test_battery_fill():
    assert theme.battery_fill_color(None, False, False) is None
    assert theme.battery_fill_color(10, False, True) == theme.C_CRIT
    assert theme.battery_fill_color(30, False, True) == theme.C_WARN
    assert theme.battery_fill_color(80, False, True) == theme.C_OK
    assert theme.battery_fill_color(50, True, True) == theme.C_CHARGE
    assert theme.battery_fill_units(None) == 0
    assert theme.battery_fill_units(0) >= 1
    assert theme.battery_fill_units(100, 14) == 14
    assert 1 <= theme.battery_fill_units(50, 14) <= 14


def test_wifi():
    assert theme.wifi_level("off", None) == 0
    assert theme.wifi_level("ap", None) == 3
    assert theme.wifi_level("scan", None) == 1
    assert theme.wifi_level("conn", -55) == 3
    assert theme.wifi_level("conn", -70) == 2
    assert theme.wifi_level("conn", -90) == 1
    assert theme.wifi_color("ap") == theme.C_ACCENT


def test_mesh():
    assert theme.mesh_level(False, 5) == 0
    assert theme.mesh_level(True, 0) == 1
    assert theme.mesh_level(True, 2) == 3
    assert theme.mesh_level(True, 10) == 3


def test_gps():
    assert theme.gps_state(False, True, 9) == "off"
    assert theme.gps_state(True, False, 0) == "search"
    assert theme.gps_state(True, True, 3) == "fix2d"
    assert theme.gps_state(True, True, 4) == "fix3d"
