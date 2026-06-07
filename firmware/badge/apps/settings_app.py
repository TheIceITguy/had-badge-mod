"""Schema-driven settings editor (supersedes the raw config_manager).

Renders the SettingsRegistry grouped, with typed editors: bools toggle, enums
cycle, ints/strings open a text box (validated on save). A few keys apply live
(network backend switch, GPS enable); the rest persist and apply on reboot.
"""
import lvgl

from apps.base_app import BaseApp
from ui.page import Page
from core import about
from core.settings import TYPE_BOOL, TYPE_ENUM

APP_NAME = "Settings"


class App(BaseApp):
    def __init__(self, name, badge):
        super().__init__(name, badge)
        self.foreground_sleep_ms = 80
        self.background_sleep_ms = 1000
        self.page = None
        self.items = []
        self.cursor = 0
        self.edit_active = False

    # --- model ----------------------------------------------------------
    def _build_items(self):
        s = self.badge.settings
        items = []
        for group in s.groups():
            for setting in s.items_in_group(group):
                items.append(setting)
        self.items = items

    def _value_str(self, setting):
        val = self.badge.settings.get(setting.key)
        if setting.type == TYPE_BOOL:
            return "on" if val else "off"
        if setting.secret:
            return "****" if val else "(unset)"
        return str(val)

    def _row(self, setting):
        return ("[%s] %s" % (setting.group, setting.label), "   " + self._value_str(setting))

    def _refresh_row(self, idx, cursor=False):
        if self.page is None or not self.items:
            return
        prefix = "> " if cursor else "   "
        self.page.message_rows.set_cell_value(idx, 1, prefix + self._value_str(self.items[idx]))

    # --- lifecycle ------------------------------------------------------
    def switch_to_foreground(self):
        super().switch_to_foreground()
        self._build_items()
        self.page = Page()
        self.page.create_infobar(("Settings  %s v%s" % (about.PROJECT_NAME, about.VERSION),
                                  "F1 edit  F5 home"))
        self.page.create_content()
        self.page.add_message_rows(max(1, len(self.items)), 170)
        self.page.populate_message_rows([self._row(s) for s in self.items])
        if self.items:
            self._refresh_row(self.cursor, cursor=True)
        self.page.create_menubar(["Edit", "", "Up", "Down", "Home"])
        self.page.replace_screen()

    def switch_to_background(self):
        self.page = None
        return super().switch_to_background()

    # --- navigation / editing ------------------------------------------
    def _move(self, delta):
        if not self.items:
            return
        self._refresh_row(self.cursor, cursor=False)
        self.cursor = max(0, min(len(self.items) - 1, self.cursor + delta))
        self._refresh_row(self.cursor, cursor=True)
        if self.page:
            self.page.scroll_down(13) if delta > 0 else self.page.scroll_up(13)

    def _apply_live(self, key, value):
        try:
            if key == "net_backend" and hasattr(self.badge, "net_router"):
                self.badge.net_router.set_backend(value)
            elif key == "gps_enabled":
                gps = self.badge.services.get("gps")
                if gps:
                    gps.enable() if value else gps.disable()
        except Exception as exc:  # noqa: BLE001
            print("settings apply-live:", exc)

    def _commit(self, setting, value):
        try:
            self.badge.settings.set(setting.key, value)
            self._apply_live(setting.key, self.badge.settings.get(setting.key))
            self.page.infobar_right.set_text("saved (reboot to apply some)")
        except Exception as exc:  # noqa: BLE001
            self.page.infobar_right.set_text("error: %s" % exc)
        self._refresh_row(self.cursor, cursor=True)

    def _edit_current(self):
        if not self.items:
            return
        setting = self.items[self.cursor]
        cur = self.badge.settings.get(setting.key)
        if setting.type == TYPE_BOOL:
            self._commit(setting, not cur)
        elif setting.type == TYPE_ENUM and setting.choices:
            try:
                idx = setting.choices.index(cur)
            except ValueError:
                idx = -1
            self._commit(setting, setting.choices[(idx + 1) % len(setting.choices)])
        else:
            default = "" if setting.secret else str(cur)
            self.page.create_text_box(default, one_line=True)
            self.edit_active = True
            self.page.infobar_right.set_text("type, Enter to save, Esc cancel")

    def run_foreground(self):
        if self.edit_active:
            key, _text = self.page.text_box_type(self.badge.keyboard)
            if self.badge.keyboard.escape_pressed:
                self.page.close_text_box()
                self.edit_active = False
                self._refresh_row(self.cursor, cursor=True)
                return
            if key == self.badge.keyboard.ENTER or self.badge.keyboard.f1():
                new_value = self.page.close_text_box()
                self.edit_active = False
                self._commit(self.items[self.cursor], new_value)
            return

        if self.badge.keyboard.f5():
            self.switch_to_background()
            return
        k = self.badge.keyboard.read_key()
        if k == self.badge.keyboard.UP or self.badge.keyboard.f3():
            self._move(-1)
        elif k == self.badge.keyboard.DOWN or self.badge.keyboard.f4():
            self._move(1)
        if self.badge.keyboard.f1():
            self._edit_current()
