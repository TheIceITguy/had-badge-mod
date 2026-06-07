"""Host tests for the minimal protobuf codec."""
import struct

from net.mesh import protobuf as pb


def test_varint_roundtrip():
    for v in [0, 1, 127, 128, 300, 16384, 2 ** 31 - 1, 2 ** 32 - 1, 2 ** 53]:
        enc = pb.encode_varint(v)
        dec, pos = pb.decode_varint(enc, 0)
        assert dec == v
        assert pos == len(enc)


def test_varint_negative_is_64bit():
    enc = pb.encode_varint(-1)
    assert len(enc) == 10  # int32 negative -> 64-bit two's complement
    dec, _ = pb.decode_varint(enc, 0)
    assert pb.to_signed32(dec) == -1


def test_to_signed32():
    assert pb.to_signed32(0) == 0
    assert pb.to_signed32(0xFFFFFFFF) == -1
    assert pb.to_signed32(0x80000000) == -2147483648
    assert pb.to_signed32(0x7FFFFFFF) == 2147483647


def test_fixed32_signed_and_unsigned():
    b = pb.field_fixed32(1, 0xDEADBEEF)
    field, wire, val = next(iter(pb.iter_fields(b)))
    assert field == 1 and wire == pb.WIRE_FIXED32
    assert pb.read_fixed32(val) == 0xDEADBEEF
    bs = pb.field_fixed32(2, -123456, signed=True)
    _, _, vals = next(iter(pb.iter_fields(bs)))
    assert pb.read_fixed32(vals, signed=True) == -123456


def test_bytes_and_string_fields():
    buf = pb.field_string(2, "héllo") + pb.field_bytes(4, b"\x00\x01\x02")
    out = list(pb.iter_fields(buf))
    assert out[0][0] == 2 and out[0][2].decode("utf-8") == "héllo"
    assert out[1][0] == 4 and out[1][2] == b"\x00\x01\x02"


def test_iter_skips_unknown_fields():
    # field 1 varint, an unknown field 9 fixed64, field 2 string
    buf = (pb.field_varint(1, 42)
           + pb._tag(9, pb.WIRE_FIXED64) + struct.pack("<Q", 7)
           + pb.field_string(2, "ok"))
    got = {f: v for f, w, v in pb.iter_fields(buf)}
    assert got[1] == 42
    assert got[2].decode() == "ok"
    assert 9 in got  # unknown field is surfaced, not a parse error
