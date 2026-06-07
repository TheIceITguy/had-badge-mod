"""Minimal MicroPython ``machine`` shim for host-side tests."""


def unique_id():
    return b"\x00\x11\x22\x33\x44\x55"


def reset():
    pass


def freq(*a):
    return 240_000_000


class Pin:
    IN = 0
    OUT = 1
    PULL_UP = 2
    PULL_DOWN = 3

    def __init__(self, *a, **k):
        self._v = 0

    def value(self, *a):
        if a:
            self._v = a[0]
            return None
        return self._v

    def on(self):
        self._v = 1

    def off(self):
        self._v = 0


class UART:
    def __init__(self, *a, **k):
        self._buf = b""

    def init(self, *a, **k):
        pass

    def any(self):
        return len(self._buf)

    def read(self, n=None):
        data, self._buf = self._buf, b""
        return data or None

    def readline(self):
        if b"\n" in self._buf:
            line, _, self._buf = self._buf.partition(b"\n")
            return line + b"\n"
        return None

    def write(self, b):
        return len(b)


class ADC:
    ATTN_11DB = 3

    def __init__(self, *a, **k):
        pass

    def atten(self, *a):
        pass

    def read_u16(self):
        return 0

    def read(self):
        return 0


class I2C:
    def __init__(self, *a, **k):
        pass

    def scan(self):
        return []

    def readfrom(self, *a, **k):
        return b""

    def writeto(self, *a, **k):
        return 0


class RTC:
    def __init__(self, *a, **k):
        self._dt = (2026, 1, 1, 0, 0, 0, 0, 0)

    def datetime(self, *a):
        if a:
            self._dt = a[0]
            return None
        return self._dt


class Timer:
    def __init__(self, *a, **k):
        pass

    def init(self, *a, **k):
        pass

    def deinit(self):
        pass
