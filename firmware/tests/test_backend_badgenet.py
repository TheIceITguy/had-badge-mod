"""Host test: BadgeNet backend Message <-> NetworkFrame round-trip."""
from net.backend import KIND_TEXT
from net.backend_badgenet import BadgeNetBackend, TEXT_CHAT
from net.protocols import NetworkFrame


class FakeSettings:
    def __init__(self, d=None):
        self.d = dict(d or {})

    def get(self, k, default=None):
        return self.d.get(k, default)


class FakeBadge:
    def __init__(self):
        self.lora = object()          # no reconfigure -> activate() is a no-op path
        self.settings = FakeSettings({"alias": "Lecco", "chat_ttl": 5})

    def device_name(self):
        return "Badge-LECCO"


def test_text_roundtrip_through_wire():
    be = BadgeNetBackend(FakeBadge())
    frame = be.text_to_frame("hello mesh", channel=101, alias="Lecco", ttl=5)
    wire = frame.serialize()  # produces self.frame too
    assert frame.frame and len(frame.frame) <= 250

    # Decode the way the network stack does on the receiving side.
    rx = NetworkFrame().set_frame(frame.frame).validate_frame()
    rx.deserialize({TEXT_CHAT.port: TEXT_CHAT})
    msg = BadgeNetBackend.frame_to_message(rx)
    assert msg.kind == KIND_TEXT
    assert msg.channel == 101
    assert msg.text == "hello mesh"
    assert msg.from_name == "Lecco"


def test_alias_falls_back_to_device_name():
    badge = FakeBadge()
    badge.settings.d["alias"] = ""   # no alias set
    be = BadgeNetBackend(badge)
    assert be._alias() == "Badge-LECC"  # device name, truncated to 10
