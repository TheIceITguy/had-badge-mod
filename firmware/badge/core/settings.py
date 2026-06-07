"""Schema-driven settings, persisted through the existing Config KV store.

Each Setting declares a key, a type (str/int/bool/enum), a default, a label and a
group, plus optional validation. Services and apps register their settings at
boot; the on-device settings app and the WebUI render the schema and edit values.
Values are stored as plain strings in the Config btree (read back as bytes), which
is back-compatible with the keys the stock firmware already writes
(e.g. ``radio_tx_power=b'9'``, ``nametag_show_image=b'false'``).
"""

TYPE_STR = "str"
TYPE_INT = "int"
TYPE_BOOL = "bool"
TYPE_ENUM = "enum"

_TRUE = ("1", "true", "yes", "on")


class Setting:
    def __init__(self, key, type=TYPE_STR, default="", label=None, group="General",
                 choices=None, minv=None, maxv=None, validate=None, secret=False,
                 help=None):
        self.key = key
        self.type = type
        self.default = default
        self.label = label or key
        self.group = group
        self.choices = list(choices) if choices else None
        self.minv = minv
        self.maxv = maxv
        self.validate = validate
        self.secret = secret
        self.help = help


def _to_str(raw):
    if raw is None:
        return None
    if isinstance(raw, (bytes, bytearray)):
        return bytes(raw).decode("utf-8", "replace")
    return str(raw)


class SettingsRegistry:
    def __init__(self, config):
        self.config = config
        self._settings = {}
        self._order = []

    # --- registration ---------------------------------------------------
    def register(self, setting):
        if setting.key not in self._settings:
            self._order.append(setting.key)
        self._settings[setting.key] = setting
        return setting

    def register_many(self, settings):
        for s in settings:
            self.register(s)

    def has(self, key):
        return key in self._settings

    def spec(self, key):
        return self._settings.get(key)

    # --- typed access ---------------------------------------------------
    def get(self, key, default=None):
        setting = self._settings.get(key)
        raw = _to_str(self.config.get(key, None))
        if setting is None:
            return raw if raw is not None else default
        if raw is None:
            return setting.default
        return self._decode(setting, raw)

    def set(self, key, value):
        setting = self._settings.get(key)
        if setting is None:
            # Unregistered key: store as string, best effort.
            self.config.set(key, str(value))
            self.config.flush()
            return
        value = self._validate(setting, value)
        self.config.set(key, self._encode(setting, value))
        self.config.flush()

    # --- introspection (settings app / WebUI) ---------------------------
    def groups(self):
        seen = []
        for key in self._order:
            g = self._settings[key].group
            if g not in seen:
                seen.append(g)
        return seen

    def items_in_group(self, group):
        return [self._settings[k] for k in self._order if self._settings[k].group == group]

    def all(self):
        return [self._settings[k] for k in self._order]

    def as_dict(self):
        out = {}
        for key in self._order:
            s = self._settings[key]
            val = self.get(key)
            out[key] = {
                "type": s.type,
                "label": s.label,
                "group": s.group,
                "value": ("••••" if s.secret and val else val),
                "choices": s.choices,
                "secret": s.secret,
                "help": s.help,
            }
        return out

    def update_from_dict(self, data):
        """Apply a {key: value} dict; return a list of 'key: error' strings."""
        errors = []
        for key, value in data.items():
            if key not in self._settings:
                continue
            try:
                self.set(key, value)
            except Exception as exc:  # noqa: BLE001
                errors.append("%s: %s" % (key, exc))
        return errors

    # --- encode / decode / validate ------------------------------------
    def _decode(self, setting, s):
        t = setting.type
        if t == TYPE_INT:
            try:
                return int(s.strip())
            except (ValueError, AttributeError):
                return setting.default
        if t == TYPE_BOOL:
            return s.strip().lower() in _TRUE
        if t == TYPE_ENUM:
            return s if (not setting.choices or s in setting.choices) else setting.default
        return s

    def _encode(self, setting, value):
        t = setting.type
        if t == TYPE_INT:
            return str(int(value))
        if t == TYPE_BOOL:
            return "true" if value else "false"
        return str(value)

    def _validate(self, setting, value):
        t = setting.type
        if t == TYPE_INT:
            try:
                value = int(value)
            except (ValueError, TypeError):
                raise ValueError("not an integer")
            if setting.minv is not None and value < setting.minv:
                raise ValueError("min %s" % setting.minv)
            if setting.maxv is not None and value > setting.maxv:
                raise ValueError("max %s" % setting.maxv)
        elif t == TYPE_BOOL:
            if isinstance(value, str):
                value = value.strip().lower() in _TRUE
            else:
                value = bool(value)
        elif t == TYPE_ENUM:
            value = str(value)
            if setting.choices and value not in setting.choices:
                raise ValueError("must be one of %s" % (setting.choices,))
        else:
            value = str(value)
        if setting.validate and not setting.validate(value):
            raise ValueError("invalid value")
        return value
