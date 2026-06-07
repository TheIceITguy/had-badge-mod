"""Meshtastic MeshPacket on-air header + full packet build/parse.

Header is 16 bytes, little-endian:
  to u32 | from u32 | id u32 | flags u8 | channel u8 | next_hop u8 | relay_node u8
flags: hop_limit = f & 0x07, want_ack = f & 0x08, via_mqtt = f & 0x10,
       hop_start = (f & 0xE0) >> 5
Payload after the header = AES-CTR(Data protobuf).
"""
import struct

try:
    import random
except ImportError:  # pragma: no cover
    random = None

from net.mesh.mesh_crypto import channel_hash, build_nonce, aes_ctr_xcrypt, expand_psk
from net.mesh.pb_messages import decode_data

BROADCAST = 0xFFFFFFFF
HEADER_LEN = 16


def gen_packet_id():
    if random is not None:
        pid = random.getrandbits(32)
    else:  # pragma: no cover
        import os
        pid = int.from_bytes(os.urandom(4), "big")
    return pid or 1


def build_flags(hop_limit=3, hop_start=3, want_ack=False, via_mqtt=False):
    return ((hop_limit & 0x07)
            | (0x08 if want_ack else 0)
            | (0x10 if via_mqtt else 0)
            | ((hop_start & 0x07) << 5))


def build_header(to, frm, pid, hop_limit=3, hop_start=3, want_ack=False,
                 via_mqtt=False, ch_hash=0, next_hop=0, relay_node=0):
    flags = build_flags(hop_limit, hop_start, want_ack, via_mqtt)
    return struct.pack("<IIIBBBB", to & 0xFFFFFFFF, frm & 0xFFFFFFFF,
                       pid & 0xFFFFFFFF, flags, ch_hash & 0xFF,
                       next_hop & 0xFF, relay_node & 0xFF)


def parse_header(frame):
    to, frm, pid, flags, ch, next_hop, relay = struct.unpack("<IIIBBBB", frame[:HEADER_LEN])
    return {
        "to": to, "from": frm, "id": pid,
        "flags": flags,
        "hop_limit": flags & 0x07,
        "want_ack": bool(flags & 0x08),
        "via_mqtt": bool(flags & 0x10),
        "hop_start": (flags & 0xE0) >> 5,
        "channel": ch, "next_hop": next_hop, "relay_node": relay,
    }


def build_packet(to, frm, pid, ch_name, psk, data_bytes,
                 hop_limit=3, hop_start=3, want_ack=False):
    ch = channel_hash(ch_name, psk)
    header = build_header(to, frm, pid, hop_limit=hop_limit, hop_start=hop_start,
                          want_ack=want_ack, ch_hash=ch, relay_node=frm)
    nonce = build_nonce(pid, frm)
    ct = aes_ctr_xcrypt(expand_psk(psk), nonce, data_bytes)
    return header + ct


def parse_packet(frame, ch_name, psk):
    """Return (header_dict, data_dict) if the frame is on our channel and decodes,
    else None. Frames for other channels or PKI packets are silently ignored."""
    if frame is None or len(frame) < HEADER_LEN:
        return None
    hdr = parse_header(frame)
    if hdr["channel"] != channel_hash(ch_name, psk):
        return None
    nonce = build_nonce(hdr["id"], hdr["from"])
    pt = aes_ctr_xcrypt(expand_psk(psk), nonce, frame[HEADER_LEN:])
    try:
        data = decode_data(pt)
    except Exception:  # noqa: BLE001
        return None
    # A valid Data has a known portnum; portnum 0 with empty payload is suspect.
    if data["portnum"] == 0 and not data["payload"]:
        return None
    return hdr, data
