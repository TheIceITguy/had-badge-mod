"""Host tests for the pure settings-UI helpers."""
from apps.settings_logic import render_value, next_enum, step_int
from core.settings import Setting, TYPE_BOOL, TYPE_ENUM, TYPE_INT, TYPE_STR


def test_render_value():
    assert render_value(Setting("k", TYPE_BOOL), True) == "on"
    assert render_value(Setting("k", TYPE_BOOL), False) == "off"
    assert render_value(Setting("k", TYPE_INT), 17) == "17"
    assert render_value(Setting("k", TYPE_STR), "hi") == "hi"
    sec = Setting("k", TYPE_STR, secret=True)
    assert render_value(sec, "pw") == "••••"
    assert render_value(sec, "") == ""   # empty secret not masked


def test_next_enum():
    c = ["a", "b", "c"]
    assert next_enum(c, "a") == "b"
    assert next_enum(c, "c") == "a"        # wrap
    assert next_enum(c, "c", -1) == "b"
    assert next_enum(c, "unknown") == "b"  # unknown -> index 0 -> +1


def test_step_int():
    assert step_int(5, 0, 10, 1) == 6
    assert step_int(10, 0, 10, 1) == 10    # clamp hi
    assert step_int(0, 0, 10, -1) == 0     # clamp lo
    assert step_int(None, 0, 10, 1) == 1   # None -> 0
    assert step_int(5, None, None, 5) == 10
