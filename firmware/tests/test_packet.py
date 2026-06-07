"""Host tests for MeshPacket header + full build/parse."""
from net.mesh import packet as pkt
from net.mesh import pb_messages as m


def test_header_roundtrip_layout():
    h = pkt.build_header(0xFFFFFFFF, 0x12345678, 0xDEADBEEF,
                         hop_limit=3, hop_start=3, ch_hash=0x08, relay_node=0x78)
    assert len(h) == 16
    p = pkt.parse_header(h)
    assert p["to"] == 0xFFFFFFFF
    assert p["from"] == 0x12345678
    assert p["id"] == 0xDEADBEEF
    assert p["hop_limit"] == 3
    assert p["hop_start"] == 3
    assert p["channel"] == 0x08
    assert p["relay_node"] == 0x78
    # little-endian 'to' in the first 4 bytes
    assert h[0:4] == b"\xff\xff\xff\xff"
    assert h[4:8] == b"\x78\x56\x34\x12"


def test_flags_default_longfast():
    assert pkt.build_flags(hop_limit=3, hop_start=3) == 0x63


def test_full_packet_text_roundtrip():
    psk = b"\x01"  # default channel
    frm = 0x12345678
    pid = pkt.gen_packet_id()
    data = m.encode_data(m.TEXT_MESSAGE_APP, "ping from badge".encode())
    frame = pkt.build_packet(pkt.BROADCAST, frm, pid, "LongFast", psk, data)
    # header channel hint must be the default LongFast hash
    assert frame[13] == 0x08
    out = pkt.parse_packet(frame, "LongFast", psk)
    assert out is not None
    hdr, dec = out
    assert hdr["from"] == frm and hdr["id"] == pid
    assert dec["portnum"] == m.TEXT_MESSAGE_APP
    assert dec["payload"].decode() == "ping from badge"


def test_full_packet_position_roundtrip():
    psk = b"\x01"
    pid = pkt.gen_packet_id()
    pos = m.encode_position(45.8566, 9.3976, alt_m=214, ts=1750000000, sats=8)
    data = m.encode_data(m.POSITION_APP, pos)
    frame = pkt.build_packet(pkt.BROADCAST, 0xAABBCCDD, pid, "LongFast", psk, data)
    hdr, dec = pkt.parse_packet(frame, "LongFast", psk)
    assert dec["portnum"] == m.POSITION_APP
    p = m.decode_position(dec["payload"])
    assert abs(p["lat"] - 45.8566) < 1e-7 and abs(p["lon"] - 9.3976) < 1e-7


def test_parse_rejects_wrong_channel():
    psk = b"\x01"
    data = m.encode_data(m.TEXT_MESSAGE_APP, b"secret")
    frame = pkt.build_packet(pkt.BROADCAST, 1, pkt.gen_packet_id(), "LongFast", psk, data)
    # A receiver on a different channel name gets a different hash -> ignored.
    assert pkt.parse_packet(frame, "AnotherChan", psk) is None


def test_parse_rejects_short_frame():
    assert pkt.parse_packet(b"\x00" * 8, "LongFast", b"\x01") is None
    assert pkt.parse_packet(None, "LongFast", b"\x01") is None
