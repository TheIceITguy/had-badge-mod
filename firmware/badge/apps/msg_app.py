"""Backend-agnostic messaging app.

Sends/receives text through badge.net_router, so it works over whichever LoRa
stack is active (Meshtastic by default, or BadgeNet). Incoming messages arrive via
the EventBus; the handler only buffers them (fast / no-LVGL) and the foreground
loop renders.
"""
from collections import deque

from apps.base_app import BaseApp
from ui.chat import Chat
from core.events import EV_MESSAGE_RECEIVED, EV_BACKEND_CHANGED
from net.backend import KIND_TEXT

APP_NAME = "Messages"

MAX_MESSAGE_LEN = 100
BUFFER_LEN = 100


class App(BaseApp):
    def __init__(self, name, badge):
        super().__init__(name, badge)
        self.foreground_sleep_ms = 10
        self.background_sleep_ms = 2000
        self.inbox = deque((), BUFFER_LEN)
        self.dirty = True
        self.page = None
        self.compose_active = False

    # --- event handling (fast, no LVGL) --------------------------------
    def _on_message(self, msg):
        if msg is None or msg.kind != KIND_TEXT:
            return
        self.inbox.append((self._sender_label(msg), msg.text or ""))
        self.dirty = True

    def _on_backend_changed(self, name):
        self.dirty = True

    def _sender_label(self, msg):
        if msg.from_name:
            return msg.from_name
        node = self.badge.services.get("mesh") if hasattr(self.badge, "services") else None
        if node is not None:
            rec = node.node(msg.from_id) if hasattr(node, "node") else None
            if rec and getattr(rec, "short_name", None):
                return rec.short_name
        return "%08x" % (msg.from_id or 0)

    def start(self):
        super().start()
        self.badge.events.subscribe(EV_MESSAGE_RECEIVED, self._on_message)
        self.badge.events.subscribe(EV_BACKEND_CHANGED, self._on_backend_changed)

    # --- UI ------------------------------------------------------------
    def _backend_name(self):
        router = self.badge.services.get("net") if hasattr(self.badge, "services") else None
        return router.active_name if router else "?"

    def _render(self):
        if self.page is not None:
            self.page.populate_message_rows(list(self.inbox))
            self.dirty = False

    def switch_to_foreground(self):
        super().switch_to_foreground()
        self.page = Chat(
            infobar_contents=("Messages [%s]" % self._backend_name(), self.badge.device_name()),
            menubar_labels=("Post", "Latest", "", "", "Home"),
            messages=[],
        )
        self.page.add_message_rows(1, left_width=80)
        self.dirty = True
        self._render()
        self.page.replace_screen()

    def switch_to_background(self):
        self.page = None
        return super().switch_to_background()

    def run_foreground(self):
        if self.compose_active:
            key, text = self.page.text_box_type(self.badge.keyboard)
            self.page.infobar_right.set_text("%d/%d  F1 send" % (len(text), MAX_MESSAGE_LEN))
            if self.badge.keyboard.escape_pressed:
                self.page.close_text_box()
                self.compose_active = False
                self.page.infobar_right.set_text(self.badge.device_name())
            elif self.badge.keyboard.f1():
                if self.page.text_box.get_text():
                    message_text = self.page.close_text_box()
                    self.badge.net_router.send_text(message_text[:MAX_MESSAGE_LEN])
                    self.inbox.append(("me", message_text[:MAX_MESSAGE_LEN]))
                    self.dirty = True
                self.compose_active = False
                self.page.infobar_right.set_text(self.badge.device_name())
            return

        if self.dirty:
            self._render()

        if self.badge.keyboard.f5():
            self.switch_to_background()
            return
        if self.badge.keyboard.f1():
            self.page.create_text_box()
            self.compose_active = True
        elif self.badge.keyboard.f2():
            self.page.scroll_bottom()
        else:
            key = self.badge.keyboard.read_key()
            if key == self.badge.keyboard.UP:
                self.page.scroll_up(13)
            elif key == self.badge.keyboard.DOWN:
                self.page.scroll_down(13)
