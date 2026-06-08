"""WhatsApp-style messaging.

A scrolling list of chat bubbles with an always-on input docked at the bottom:
type immediately, Enter sends, Up/Down scroll history, Esc goes home. Works over
whichever LoRa stack is active (Meshtastic by default, or BadgeNet).
"""
import time
from collections import deque

import lvgl

from apps.base_app import BaseApp
from ui.frame import Frame
from ui import theme
from core.events import EV_MESSAGE_RECEIVED, EV_BACKEND_CHANGED
from net.backend import KIND_TEXT

APP_NAME = "Messages"

MAX_MESSAGE_LEN = 200
BUFFER_LEN = 100
BUBBLE_MAXW = 250
_hx = lvgl.color_hex


def _now():
    try:
        t = time.localtime()
        return "%02d:%02d" % (t[3], t[4])
    except Exception:  # noqa: BLE001
        return ""


class App(BaseApp):
    def __init__(self, name, badge):
        super().__init__(name, badge)
        self.foreground_sleep_ms = 10
        self.background_sleep_ms = 2000
        self.inbox = deque((), BUFFER_LEN)
        self.dirty = True
        self.following = True
        self.fr = None
        self._bubbles = []

    # --- events (fast, no LVGL) ----------------------------------------
    def _on_message(self, msg):
        if msg is None or msg.kind != KIND_TEXT:
            return
        self.inbox.append((self._sender_label(msg), msg.text or "", _now()))
        self.dirty = True

    def _on_backend(self, _name):
        self.dirty = True

    def _sender_label(self, msg):
        if msg.from_name:
            return msg.from_name
        mesh = self.badge.services.get("mesh") if hasattr(self.badge, "services") else None
        if mesh is not None and hasattr(mesh, "node"):
            rec = mesh.node(msg.from_id)
            if rec is not None and rec.short_name:
                return rec.short_name
        return "%08x" % (msg.from_id or 0)

    def start(self):
        super().start()
        self.badge.events.subscribe(EV_MESSAGE_RECEIVED, self._on_message)
        self.badge.events.subscribe(EV_BACKEND_CHANGED, self._on_backend)

    # --- bubble rendering ----------------------------------------------
    def _backend_name(self):
        router = self.badge.services.get("net") if hasattr(self.badge, "services") else None
        return router.active_name if router else "?"

    def _add_bubble(self, mine, text):
        body = self.fr.body
        lbl = lvgl.label(body)
        lbl.add_style(theme.st_bubble_me if mine else theme.st_bubble_them, 0)
        lbl.set_text(text)
        body.update_layout()
        if lbl.get_width() > BUBBLE_MAXW:
            lbl.set_width(BUBBLE_MAXW)
            body.update_layout()
        w = lbl.get_width()
        h = lbl.get_height()
        inner = theme.CONTENT_W - 2 * theme.PAD_M
        lbl.set_pos((inner - w) if mine else 0, self._y)
        self._y += h + 4
        self._bubbles.append(lbl)

    def _clear_bubbles(self):
        for b in self._bubbles:
            try:
                b.delete()
            except Exception:  # noqa: BLE001
                pass
        self._bubbles = []
        self._y = 0

    def _render(self):
        if self.fr is None:
            return
        self._clear_bubbles()
        if not self.inbox:
            hint = lvgl.label(self.fr.body)
            hint.set_style_text_color(_hx(theme.C_TEXT_DIM), 0)
            hint.set_text("No messages yet.\nType below and press Enter.")
            hint.align(lvgl.ALIGN.CENTER, 0, 0)
            self._bubbles.append(hint)
            self.dirty = False
            return
        for (who, text, _ts) in self.inbox:
            self._add_bubble(who == "me", text)
        self.dirty = False

    def _scroll_bottom(self):
        body = self.fr.body
        try:
            dy = body.get_scroll_bottom()
            if dy > 0:
                body.scroll_by_bounded(0, -dy, False)
        except Exception:  # noqa: BLE001
            pass

    def _at_bottom(self):
        try:
            return self.fr.body.get_scroll_bottom() <= 2
        except Exception:  # noqa: BLE001
            return True

    # --- lifecycle ------------------------------------------------------
    def switch_to_foreground(self):
        super().switch_to_foreground()
        self.fr = Frame("Messages", "%s  %s" % (self._backend_name(), self.badge.device_name()))
        self.fr.make_input("Message")
        self.dirty = True
        self._render()
        self._scroll_bottom()
        self.following = True
        self.fr.replace_screen()

    def switch_to_background(self):
        self.fr = None
        self._bubbles = []
        return super().switch_to_background()

    def run_foreground(self):
        kb = self.badge.keyboard
        if kb.esc():
            self.switch_to_background()
            return
        if self.dirty:
            follow = self.following and self._at_bottom()
            self._render()
            if follow:
                self._scroll_bottom()
        key = kb.read_key()
        if key is None:
            return
        if key == kb.ENTER:
            text = self.fr.input_text()
            if text:
                self.badge.net_router.send_text(text[:MAX_MESSAGE_LEN])
                self.inbox.append(("me", text[:MAX_MESSAGE_LEN], _now()))
                self.dirty = True
                self.following = True
            self.fr.clear_input()
        elif key == kb.UP:
            self.fr.body.scroll_by_bounded(0, 20, False)
            self.following = False
        elif key == kb.DOWN:
            self.fr.body.scroll_by_bounded(0, -20, False)
            self.following = self._at_bottom()
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
