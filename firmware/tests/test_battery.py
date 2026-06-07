"""Host tests for the battery voltage->percent curve."""
from services.battery_service import volts_to_pct


def test_endpoints_and_clamp():
    assert volts_to_pct(4.20) == 100
    assert volts_to_pct(3.30) == 0
    assert volts_to_pct(5.0) == 100   # clamped
    assert volts_to_pct(2.0) == 0     # clamped


def test_midpoint():
    assert 45 <= volts_to_pct(3.75) <= 55


def test_none_is_none():
    assert volts_to_pct(None) is None
