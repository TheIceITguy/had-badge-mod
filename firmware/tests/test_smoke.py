"""Smoke test: the deployable tree and import paths are wired up."""
import os


def test_badge_tree_present():
    here = os.path.dirname(os.path.abspath(__file__))
    badge = os.path.normpath(os.path.join(here, "..", "badge"))
    assert os.path.isfile(os.path.join(badge, "main.py"))
    assert os.path.isdir(os.path.join(badge, "net"))


def test_micropython_shims_importable():
    # These have no real host implementation; the shims must satisfy the import.
    import machine  # noqa: F401
    import btree  # noqa: F401
    import network  # noqa: F401
    assert isinstance(machine.unique_id(), (bytes, bytearray))
