"""Host tests for app manifest resolution and legacy name precedence."""
import types

from core.manifest import AppManifest, manifest_for, resolve_app_name


def _module(name, **attrs):
    m = types.ModuleType(name)
    for k, v in attrs.items():
        setattr(m, k, v)
    return m


def test_app_name_module_variable_wins():
    mod = _module("apps.snake", APP_NAME="Snake!")

    class App:
        pass

    assert resolve_app_name(mod, App) == "Snake!"
    assert manifest_for(mod, App).name == "Snake!"


def test_class_named_App_uses_filename_capitalised():
    mod = _module("apps.game_of_life")

    class App:
        pass

    # base "game_of_life" -> capitalise first letter, truncate to 9
    assert resolve_app_name(mod, App) == "Game_of_l"


def test_custom_class_name_truncated_to_9():
    mod = _module("apps.whatever")

    class SuperLongAppName:
        pass

    assert resolve_app_name(mod, SuperLongAppName) == "SuperLong"


def test_short_class_name_kept():
    mod = _module("apps.x")

    class SnakeApp:
        pass

    assert resolve_app_name(mod, SnakeApp) == "SnakeApp"


def test_explicit_manifest_takes_precedence():
    mod = _module("apps.gps", APP_NAME="ignored")

    class App:
        MANIFEST = AppManifest(name="GPS", requires=("gps",), version="2.0")

    man = manifest_for(mod, App)
    assert man.name == "GPS"
    assert man.requires == ("gps",)
    assert man.version == "2.0"
