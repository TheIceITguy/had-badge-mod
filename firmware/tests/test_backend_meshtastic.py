"""Host tests for the Meshtastic backend's pure logic (no radio, no asyncio loop)."""
from core.settings import SettingsRegistry
from net.mesh.backend_meshtastic import MeshtasticBackend
from net.mesh.packet import parse_header, parse_packet


class FakeConfig:
    def __init__(self):
        self.d = {}

    def get(self, k, default=None):
        return self.d.get(k, default)

    def set(self, k, v):
        self.d[k] = v

    def flush(self):
        pass


class FakeLora:
    def get_snr(self):
        return 5.5

    def get_rssi(self):
        return -42

    def reconfigure(self, **kw):
        self.cfg = kw


class FakeBadge:
    def __init__(self, node_id):
        self.node_id = node_id
        self.settings = SettingsRegistry(FakeConfig())
        self.lora = FakeLora()

    def device_name(self):
        return "Badge-%04X" % (self.node_id & 0xFFFF)


def make(node_id):
    return MeshtasticBackend(FakeBadge(node_id))


def test_radio_params_default_eu868_longfast():
    p = make(0xA0000001).radio_params()
    assert abs(p["freq"] - 869.525) < 1e-6
    assert p["sync"] == 0x2B
    assert (p["bw"], p["sf"], p["cr"]) == (250.0, 11, 5)


def test_text_frame_parses_back():
    be = make(0x12345678)
    frame = be._build_text_frame("hello mesh")
    hdr, data = parse_packet(frame, be.channel_name, be.psk)
    assert hdr["from"] == 0x12345678
    assert frame[13] == 0x08  # default LongFast channel hash
    from net.mesh import pb_messages as pbm
    assert data["portnum"] == pbm.TEXT_MESSAGE_APP
    assert data["payload"].decode() == "hello mesh"


def test_handle_foreign_text_emits_and_rebroadcasts():
    a = make(0xA0000001)
    a.rebroadcast = True   # router mode
    b = make(0xB0000002)
    frame = b._build_text_frame("hi from B")
    msg, rebroadcast = a._handle_frame(frame, now=100)
    assert msg.kind == "text" and msg.text == "hi from B"
    assert msg.from_id == 0xB0000002
    assert msg.snr == 5.5 and msg.rssi == -42
    assert rebroadcast is not None
    assert parse_header(rebroadcast)["hop_limit"] == 2          # 3 -> 2
    assert parse_header(rebroadcast)["relay_node"] == a.my_node & 0xFF


def test_no_rebroadcast_by_default():
    a = make(0xA0000001)            # mesh_rebroadcast defaults False (client mode)
    b = make(0xB0000002)
    msg, rebroadcast = a._handle_frame(b._build_text_frame("hi"), now=1)
    assert msg is not None and rebroadcast is None


def test_dedup_second_copy_ignored():
    a = make(0xA0000001)
    b = make(0xB0000002)
    frame = b._build_text_frame("dup")
    first = a._handle_frame(frame, now=1)
    second = a._handle_frame(frame, now=2)
    assert first[0] is not None
    assert second == (None, None)


def test_own_packet_ignored():
    a = make(0xA0000001)
    own = a._build_text_frame("mine")
    assert a._handle_frame(own, now=1) == (None, None)


def test_position_roundtrip():
    a = make(0xA0000001)
    b = make(0xB0000002)
    frame = b._build_position_frame(45.8566, 9.3976, alt=214, ts=1750000000, sats=8)
    msg, _ = a._handle_frame(frame, now=1)
    assert msg.kind == "position"
    assert abs(msg.lat - 45.8566) < 1e-6 and abs(msg.lon - 9.3976) < 1e-6
    assert msg.alt == 214 and msg.sats == 8


def test_nodeinfo_roundtrip():
    a = make(0xA0000001)
    b = make(0xB0000002)
    frame = b._build_nodeinfo_frame()
    msg, _ = a._handle_frame(frame, now=1)
    assert msg.kind == "nodeinfo"
    assert msg.long_name == "Badge-0002"


def test_decrement_hop_exhausted():
    a = make(0xA0000001)
    frame = bytearray(a._build_text_frame("x"))
    frame[12] = frame[12] & 0xF8  # hop_limit -> 0
    assert MeshtasticBackend.decrement_hop(bytes(frame), 0xAB) is None
