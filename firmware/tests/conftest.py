"""Pytest bootstrap for host-side tests.

Adds the deployable badge tree to sys.path so modules import as they do on the
device (e.g. ``import net.mesh.protobuf``, ``import core.events``). Real third-party
packages (notably ``cryptography``) take precedence; MicroPython-only modules
(``machine``, ``lvgl``, ``btree``, ``network``, ``uasyncio``) fall through to the
lightweight stubs in ``shims/``.
"""
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_BADGE = os.path.normpath(os.path.join(_HERE, "..", "badge"))
_SHIMS = os.path.join(_HERE, "shims")

# Badge tree first so its packages resolve like on-device.
if _BADGE not in sys.path:
    sys.path.insert(0, _BADGE)
# Shims last: only used when a real module is unavailable on the host.
if _SHIMS not in sys.path:
    sys.path.append(_SHIMS)
