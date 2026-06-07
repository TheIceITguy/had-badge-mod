"""Host tests for Data / Position / User message encode+decode."""
from net.mesh import pb_messages as m


def test_data_roundtrip_text():
    enc = m.encode_data(m.TEXT_MESSAGE_APP, "hello world".encode("utf-8"))
    d = m.decode_data(enc)
    assert d["portnum"] == m.TEXT_MESSAGE_APP
    assert d["payload"].decode("utf-8") == "hello world"


def test_data_optional_fields():
    enc = m.encode_data(m.POSITION_APP, b"\x01\x02", want_response=True,
                        dest=0xFFFFFFFF, source=0x12345678, request_id=0xABCDEF01)
    d = m.decode_data(enc)
    assert d["portnum"] == m.POSITION_APP
    assert d["payload"] == b"\x01\x02"
    assert d["want_response"] is True
    assert d["dest"] == 0xFFFFFFFF
    assert d["source"] == 0x12345678
    assert d["request_id"] == 0xABCDEF01


def test_position_roundtrip_precision():
    lat, lon = 45.8566, 9.3976  # Lecco, IT
    enc = m.encode_position(lat, lon, alt_m=214, ts=1750000000, sats=9)
    p = m.decode_position(enc)
    assert abs(p["lat"] - lat) < 1e-7
    assert abs(p["lon"] - lon) < 1e-7
    assert p["alt"] == 214
    assert p["time"] == 1750000000
    assert p["sats"] == 9


def test_position_negative_altitude_and_coords():
    enc = m.encode_position(-33.8688, 151.2093, alt_m=-12)  # below sea level
    p = m.decode_position(enc)
    assert abs(p["lat"] - (-33.8688)) < 1e-7
    assert abs(p["lon"] - 151.2093) < 1e-7
    assert p["alt"] == -12


def test_user_roundtrip():
    enc = m.encode_user("!aabbccdd", "Lecco Badge", "LBdg", hw_model=0)
    u = m.decode_user(enc)
    assert u["id"] == "!aabbccdd"
    assert u["long_name"] == "Lecco Badge"
    assert u["short_name"] == "LBdg"
