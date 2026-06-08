"""Categorized settings: a master-detail drill-down over the SettingsRegistry.

  categories  -> pick a group (Device, Radio, Network, WiFi, GPS, Power)
  items       -> toggle bools, cycle enums, step ints (Left/Right), edit str/int
  editor      -> inline text input for str/int values

Esc backs out one level. A few keys apply live (network backend, GPS enable);
the rest persist and take effect on reboot.
"""
import lvgl

from apps.base_app import BaseApp
from apps.settings_logic import render_value, next_enum, step_int
from ui.frame import Frame
from ui import theme
from core.settings import TYPE_BOOL, TYPE_ENUM, TYPE_INT

APP_NAME = "Settings"
_hx = lvgl.color_hex
ROW_H = 24
VISIBLE = 3


class App(BaseApp):
    def __init__(self, name, badge):
        super().__init__(name, badge)
        self.foreground_sleep_ms = 30
        self.background_sleep_ms = 1000
        self.fr = None
        self.view = "categories"
        self.groups = []
        self.items = []
        self.cur_group = None
        self.cat_cursor = 0
        self.item_cursor = 0
        self.rows = []
        self.vals = []
        self._scroll_rows = 0

    # --- list plumbing --------------------------------------------------
    def _reset_body_list(self):
        for r in self.rows:
            try:
                r.delete()
            except Exception:  # noqa: BLE001
                pass
        self.rows = []
        self.vals = []
        self._scroll_rows = 0
        body = self.fr.body
        body.set_flex_flow(lvgl.FLEX_FLOW.COLUMN)
        body.set_flex_align(lvgl.FLEX_ALIGN.START, lvgl.FLEX_ALIGN.START,
                            lvgl.FLEX_ALIGN.START)
        try:
            body.scroll_by_bounded(0, 9999, False)  # snap to top
        except Exception:  # noqa: BLE001
            pass

    def _add_row(self, label_text, value_text):
        inner = theme.CONTENT_W - 2 * theme.PAD_M
        r = lvgl.obj(self.fr.body)
        r.set_scrollbar_mode(0)
        r.add_style(theme.st_card, 0)
        r.set_size(inner, ROW_H)
        r.set_style_pad_top(0, 0)
        r.set_style_pad_bottom(0, 0)
        lab = lvgl.label(r)
        lab.set_text(label_text)
        lab.set_style_text_font(theme.f_body(), 0)
        lab.set_style_text_color(_hx(theme.C_TEXT), 0)
        lab.align(lvgl.ALIGN.LEFT_MID, 2, 0)
        val = lvgl.label(r)
        val.set_text(value_text)
        val.set_style_text_font(theme.f_tiny(), 0)
        val.set_style_text_color(_hx(theme.C_ACCENT), 0)
        val.align(lvgl.ALIGN.RIGHT_MID, -4, 0)
        self.rows.append(r)
        self.vals.append(val)

    def _highlight(self, sel):
        for j, r in enumerate(self.rows):
            if j == sel:
                r.set_style_bg_color(_hx(theme.C_SURFACE_2), 0)
                r.set_style_border_color(_hx(theme.C_ACCENT), 0)
                r.set_style_border_width(theme.FOCUS_RING, 0)
            else:
                r.set_style_bg_color(_hx(theme.C_SURFACE), 0)
                r.set_style_border_color(_hx(theme.C_DIVIDER), 0)
                r.set_style_border_width(theme.BORDER_HAIR, 0)
        # keep selection visible
        if sel < self._scroll_rows:
            self._scroll_rows = sel
            self.fr.body.scroll_by_bounded(0, ROW_H, False)
        elif sel >= self._scroll_rows + VISIBLE:
            self._scroll_rows = sel - VISIBLE + 1
            self.fr.body.scroll_by_bounded(0, -ROW_H, False)

    # --- views ----------------------------------------------------------
    def _show_categories(self):
        self.view = "categories"
        self.groups = self.badge.settings.groups()
        self._reset_body_list()
        self.fr.set_title("Settings")
        self.fr.set_context("%d groups" % len(self.groups))
        for g in self.groups:
            self._add_row(g, "›")
        self._highlight(self.cat_cursor)

    def _show_items(self):
        self.view = "items"
        self.items = self.badge.settings.items_in_group(self.cur_group)
        self._reset_body_list()
        self.fr.set_title(self.cur_group)
        self.fr.set_context("%d settings" % len(self.items))
        for s in self.items:
            self._add_row(s.label, render_value(s, self.badge.settings.get(s.key)))
        self.item_cursor = 0
        self._highlight(self.item_cursor)

    def _refresh_value(self, idx):
        s = self.items[idx]
        self.vals[idx].set_text(render_value(s, self.badge.settings.get(s.key)))

    def _open_editor(self, setting):
        self.view = "editor"
        self.editing = setting
        cur = self.badge.settings.get(setting.key)
        self.fr.set_title("Edit %s" % setting.label)
        rng = ""
        if setting.type == TYPE_INT and (setting.minv is not None or setting.maxv is not None):
            rng = "%s..%s" % (setting.minv, setting.maxv)
        self.fr.set_context(rng)
        self.fr.make_input(str(setting.label))
        self.fr.input_add("" if setting.secret else str(cur))

    # --- lifecycle ------------------------------------------------------
    def switch_to_foreground(self):
        super().switch_to_foreground()
        self.fr = Frame("Settings", "")
        self.fr.make_menubar(["Edit", "", "Up", "Down", "Back"])
        self._show_categories()
        self.fr.replace_screen()

    def switch_to_background(self):
        self.fr = None
        self.rows = []
        self.vals = []
        return super().switch_to_background()

    # --- helpers --------------------------------------------------------
    def _commit(self, setting, value):
        try:
            self.badge.settings.set(setting.key, value)
            self._apply_live(setting.key, self.badge.settings.get(setting.key))
        except Exception as exc:  # noqa: BLE001
            self.fr.set_context("err: %s" % exc)
            return False
        return True

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

    # --- input ----------------------------------------------------------
    def run_foreground(self):
        kb = self.badge.keyboard
        if self.view == "editor":
            self._run_editor(kb)
            return
        if kb.esc():
            if self.view == "items":
                self._show_categories()
            else:
                self.switch_to_background()
            return
        key = kb.read_key()
        if self.view == "categories":
            self._run_categories(kb, key)
        else:
            self._run_items(kb, key)

    def _run_categories(self, kb, key):
        if key == kb.UP or kb.f3():
            self.cat_cursor = max(0, self.cat_cursor - 1)
            self._highlight(self.cat_cursor)
        elif key == kb.DOWN or kb.f4():
            self.cat_cursor = min(len(self.groups) - 1, self.cat_cursor + 1)
            self._highlight(self.cat_cursor)
        elif key == kb.ENTER or kb.f1():
            self.cur_group = self.groups[self.cat_cursor]
            self._show_items()

    def _run_items(self, kb, key):
        if not self.items:
            return
        s = self.items[self.item_cursor]
        cur = self.badge.settings.get(s.key)
        if key == kb.UP or kb.f3():
            self.item_cursor = max(0, self.item_cursor - 1)
            self._highlight(self.item_cursor)
        elif key == kb.DOWN or kb.f4():
            self.item_cursor = min(len(self.items) - 1, self.item_cursor + 1)
            self._highlight(self.item_cursor)
        elif key in (kb.LEFT, kb.RIGHT):
            step = -1 if key == kb.LEFT else 1
            if s.type == TYPE_ENUM and s.choices:
                self._commit(s, next_enum(s.choices, cur, step))
                self._refresh_value(self.item_cursor)
            elif s.type == TYPE_INT:
                self._commit(s, step_int(cur, s.minv, s.maxv, step))
                self._refresh_value(self.item_cursor)
        elif key == kb.ENTER or kb.f1():
            if s.type == TYPE_BOOL:
                self._commit(s, not cur)
                self._refresh_value(self.item_cursor)
            elif s.type == TYPE_ENUM and s.choices:
                self._commit(s, next_enum(s.choices, cur, 1))
                self._refresh_value(self.item_cursor)
            else:
                self._open_editor(s)

    def _run_editor(self, kb):
        key = kb.read_key()
        if kb.esc():
            self._show_items()
            return
        if key is None:
            return
        if key == kb.ENTER:
            ok = self._commit(self.editing, self.fr.input_text())
            if ok:
                self._show_items()
        elif key == kb.BS:
            self.fr.input_backspace()
        elif key == kb.DEL:
            self.fr.input_delete_fwd()
        elif key == kb.LEFT:
            self.fr.cursor_left()
        elif key == kb.RIGHT:
            self.fr.cursor_right()
        elif key == kb.TAB:
            pass
        else:
            self.fr.input_add(key)
