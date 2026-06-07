"""Minimal ``btree`` shim (dict-backed) for host tests."""


class _DB(dict):
    def flush(self):
        pass

    def close(self):
        pass

    def keys(self):
        return list(dict.keys(self))

    def get(self, key, default=None):
        return dict.get(self, key, default)


def open(stream, *a, **k):  # noqa: A001 - mirror MicroPython API name
    return _DB()
