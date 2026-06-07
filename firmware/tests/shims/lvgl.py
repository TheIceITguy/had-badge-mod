"""Permissive ``lvgl`` shim for host tests (no real rendering).

Any attribute access or call returns a chainable dummy, so UI modules can be
imported and lightly exercised without a display.
"""


class _Dummy:
    def __init__(self, *a, **k):
        pass

    def __call__(self, *a, **k):
        return _Dummy()

    def __getattr__(self, name):
        return _Dummy()

    def __setattr__(self, name, value):
        pass

    def __getitem__(self, key):
        return _Dummy()


def __getattr__(name):  # PEP 562 module-level fallback
    return _Dummy()
