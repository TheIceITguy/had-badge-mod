"""BadgeNet backend: the stock badge-to-badge LoRa stack behind the NetBackend API.

Wraps the existing BadgeNet text-chat protocol (port 6) so backend-agnostic apps
work over it, and so it interoperates with unmodified badges. Position / nodeinfo
are not part of BadgeNet, so those sends are no-ops here (use the Meshtastic
backend for position sharing).
"""
from net.backend import NetBackend, Message, BROADCAST, KIND_TEXT
from net.net import MY_ADDRESS, register_receiver, send
from net.protocols import NetworkFrame, Protocol

MAX_MESSAGE_LEN = 100
# Identical to apps/chat.py so it shares the port and interoperates with stock badges.
TEXT_CHAT = Protocol(port=6, name="TEXT_CHAT", structdef="!H10s%ds" % MAX_MESSAGE_LEN)

DEFAULT_CHANNEL = 101  # matches the stock chat app's default freq*100+topic


class BadgeNetBackend(NetBackend):
    name = "badgenet"

    def __init__(self, badge):
        super().__init__(badge)
        radio = badge.lora
        # BadgeNet's modem parameters (captured from the boot configuration).
        self._params = dict(
            freq=getattr(radio, "frequency", 869.525),
            bw=getattr(radio, "bandwidth", 250.0),
            sf=getattr(radio, "spreading_factor", 7),
            cr=getattr(radio, "coding_rate", 5),
            sync=getattr(radio, "sync_word", 0x12),
            preamble=getattr(radio, "preamble_length", 16),
            power=getattr(radio, "tx_power", 9),
        )
        self._registered = False

    # --- helpers (pure, host-testable) ---------------------------------
    def _alias(self):
        name = self.badge.settings.get("alias", "") if hasattr(self.badge, "settings") else ""
        if not name:
            name = self.badge.device_name() if hasattr(self.badge, "device_name") else ""
        return (name or "")[:10]

    def _ttl(self):
        try:
            return int(self.badge.settings.get("chat_ttl", 3))
        except Exception:  # noqa: BLE001
            return 3

    @staticmethod
    def frame_to_message(frame):
        """Translate a received TEXT_CHAT NetworkFrame into a logical Message."""
        channel_num, alias_b, text_b = frame.payload
        alias = alias_b.strip(b"\0").decode() if isinstance(alias_b, (bytes, bytearray)) else alias_b
        text = text_b.strip(b"\0").decode() if isinstance(text_b, (bytes, bytearray)) else text_b
        return Message(KIND_TEXT, from_id=frame.source, from_name=alias,
                       text=text, channel=channel_num)

    def text_to_frame(self, text, channel, alias, ttl):
        # Pack strings as bytes so it works under both CPython and MicroPython
        # struct ('s' requires bytes on CPython); the wire bytes are identical.
        return NetworkFrame().set_fields(
            protocol=TEXT_CHAT,
            destination=BROADCAST,
            ttl=ttl,
            payload=(channel or DEFAULT_CHANNEL, alias[:10].encode("utf-8"),
                     text.encode("utf-8")),
        )

    # --- NetBackend API ------------------------------------------------
    def activate(self):
        if hasattr(self.badge.lora, "reconfigure"):
            self.badge.lora.reconfigure(**self._params)
        if not self._registered:
            register_receiver(TEXT_CHAT, self._on_text)
            self._registered = True

    def deactivate(self):
        # BadgeNet's recv/tx tasks belong to the global badgenet singleton; the
        # Meshtastic backend pauses them on activate(). Nothing to do here.
        pass

    def _on_text(self, frame):
        try:
            self._emit(self.frame_to_message(frame))
        except Exception as exc:  # noqa: BLE001
            print("badgenet: rx decode error: %s" % exc)

    def send_text(self, text, channel=0, to_id=BROADCAST):
        send(self.text_to_frame(text, channel, self._alias(), self._ttl()))
        return True

    def my_node_id(self):
        return MY_ADDRESS

    def status(self):
        return {"name": self.name, "node": "%08x" % MY_ADDRESS, "params": self._params}
