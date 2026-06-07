"""App manifest / SDK — optional metadata an app may expose.

Apps may set a class attribute ``MANIFEST = AppManifest(...)`` (or a module-level
one). Everything is optional and fully back-compatible: apps with no manifest get
one synthesised from the legacy naming rules the stock AppManager uses, so every
existing app keeps loading unchanged.

Name precedence (highest first), matching the stock app_manager:
  1. an explicit AppManifest.name
  2. a module-level ``APP_NAME`` string
  3. the BaseApp subclass name (truncated to 9 chars); if the class is literally
     ``App`` the module file name is used (capitalised).
"""


class AppManifest:
    def __init__(self, name, version="1.0", icon=None, requires=(),
                 category="apps", description=""):
        self.name = name
        self.version = version
        self.icon = icon                      # path to an image, or None
        self.requires = tuple(requires)       # service names this app needs
        self.category = category
        self.description = description

    def __repr__(self):
        return "AppManifest(name=%r, requires=%r)" % (self.name, self.requires)


def resolve_app_name(module, cls):
    """Replicate the stock AppManager naming precedence (without the manifest)."""
    if module is not None and hasattr(module, "APP_NAME"):
        return module.APP_NAME
    name = cls.__name__
    if name == "App":
        base = module.__name__.split(".")[-1]
        name = base[0].upper() + base[1:]
    return name[:9]


def manifest_for(module, cls):
    """Return an AppManifest for an (app module, BaseApp subclass) pair, using an
    explicit manifest if present, else synthesising one from legacy rules."""
    man = getattr(cls, "MANIFEST", None) or getattr(module, "MANIFEST", None)
    if isinstance(man, AppManifest):
        return man
    return AppManifest(name=resolve_app_name(module, cls))
