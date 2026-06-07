"""Minimal protobuf wire-format codec (no external dependencies).

Supports exactly what the Meshtastic Data/Position/User messages need:
  wire type 0 (varint: int32/uint32/bool/enum),
  wire type 2 (length-delimited: bytes/string/embedded message),
  wire type 5 (fixed32: fixed32/sfixed32).
Wire type 1 (fixed64) is parsed (raw 8 bytes) so unknown fields can be skipped.

Pure Python; runs identically under CPython and MicroPython.
"""
import struct

WIRE_VARINT = 0
WIRE_FIXED64 = 1
WIRE_BYTES = 2
WIRE_FIXED32 = 5


def encode_varint(value):
    """LEB128. Negative ints are encoded as 64-bit two's complement (protobuf int32 rule)."""
    if value < 0:
        value &= (1 << 64) - 1
    out = bytearray()
    while True:
        b = value & 0x7F
        value >>= 7
        if value:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def decode_varint(buf, pos):
    """Return (value, new_pos)."""
    result = 0
    shift = 0
    while True:
        b = buf[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            return result, pos
        shift += 7


def to_signed32(u):
    u &= 0xFFFFFFFF
    return u - (1 << 32) if u >= (1 << 31) else u


def _tag(field_num, wire_type):
    return encode_varint((field_num << 3) | wire_type)


def field_varint(field_num, value):
    return _tag(field_num, WIRE_VARINT) + encode_varint(value)


def field_fixed32(field_num, value, signed=False):
    return _tag(field_num, WIRE_FIXED32) + struct.pack("<i" if signed else "<I", value)


def field_bytes(field_num, b):
    return _tag(field_num, WIRE_BYTES) + encode_varint(len(b)) + bytes(b)


def field_string(field_num, s):
    return field_bytes(field_num, s.encode("utf-8"))


def read_fixed32(b, signed=False):
    return struct.unpack("<i" if signed else "<I", b)[0]


def iter_fields(buf):
    """Yield (field_num, wire_type, value) for every field in a message buffer.

    value is an int for varints, a bytes object for length-delimited / fixed
    fields. Unknown fields are yielded too (callers ignore field numbers they do
    not model), so forward-compatibility is automatic.
    """
    pos = 0
    n = len(buf)
    while pos < n:
        key, pos = decode_varint(buf, pos)
        field_num = key >> 3
        wire = key & 0x07
        if wire == WIRE_VARINT:
            val, pos = decode_varint(buf, pos)
        elif wire == WIRE_BYTES:
            ln, pos = decode_varint(buf, pos)
            val = bytes(buf[pos:pos + ln])
            pos += ln
        elif wire == WIRE_FIXED32:
            val = bytes(buf[pos:pos + 4])
            pos += 4
        elif wire == WIRE_FIXED64:
            val = bytes(buf[pos:pos + 8])
            pos += 8
        else:
            raise ValueError("unsupported wire type %d at %d" % (wire, pos))
        yield field_num, wire, val
