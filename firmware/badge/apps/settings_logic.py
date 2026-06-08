"""Pure settings-UI helpers (no lvgl / hardware imports) so they unit-test on the
host. Used by apps/settings_app.py."""
from core.settings import TYPE_BOOL


def render_value(setting, value):
    if setting.secret and value:
        return "••••"
    if setting.type == TYPE_BOOL:
        return "on" if value else "off"
    return str(value)


def next_enum(choices, cur, step=1):
    i = choices.index(cur) if cur in choices else 0
    return choices[(i + step) % len(choices)]


def step_int(v, lo, hi, step):
    nv = (v or 0) + step
    if lo is not None:
        nv = max(lo, nv)
    if hi is not None:
        nv = min(hi, nv)
    return nv
