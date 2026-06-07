"""Encode/decode the Meshtastic protobuf messages this firmware uses.

Field numbers and types pinned to meshtastic/protobufs (mesh.proto, portnums.proto):
  Data:     portnum=1 varint, payload=2 bytes, want_response=3 bool,
            dest=4 fixed32, source=5 fixed32, request_id=6 fixed32,
            reply_id=7 fixed32, emoji=8 fixed32
  Position: latitude_i=1 sfixed32 (deg*1e7), longitude_i=2 sfixed32,
            altitude=3 int32 (m MSL), time=4 fixed32 (unix s),
            location_source=5 enum, ground_speed=15, ground_track=16 (1/100 deg),
            sats_in_view=19, precision_bits=23
  User:     id=1 string ("!aabbccdd"), long_name=2 string, short_name=3 string,
            macaddr=4 bytes, hw_model=5 enum, is_licensed=6 bool, role=7 enum
"""
from net.mesh import protobuf as pb

# PortNum (portnums.proto)
TEXT_MESSAGE_APP = 1
POSITION_APP = 3
NODEINFO_APP = 4
ROUTING_APP = 5
ADMIN_APP = 6
TELEMETRY_APP = 67
TRACEROUTE_APP = 70

# Position.LocSource
LOC_UNSET = 0
LOC_MANUAL = 1
LOC_INTERNAL = 2
LOC_EXTERNAL = 3


# --- Data ---------------------------------------------------------------
def encode_data(portnum, payload, want_response=False, dest=0, source=0,
                request_id=0, reply_id=0, emoji=0):
    out = pb.field_varint(1, portnum) + pb.field_bytes(2, payload)
    if want_response:
        out += pb.field_varint(3, 1)
    if dest:
        out += pb.field_fixed32(4, dest)
    if source:
        out += pb.field_fixed32(5, source)
    if request_id:
        out += pb.field_fixed32(6, request_id)
    if reply_id:
        out += pb.field_fixed32(7, reply_id)
    if emoji:
        out += pb.field_fixed32(8, emoji)
    return out


def decode_data(buf):
    d = {"portnum": 0, "payload": b"", "want_response": False,
         "dest": 0, "source": 0, "request_id": 0, "reply_id": 0, "emoji": 0}
    for field, wire, val in pb.iter_fields(buf):
        if field == 1:
            d["portnum"] = val
        elif field == 2:
            d["payload"] = val
        elif field == 3:
            d["want_response"] = bool(val)
        elif field == 4:
            d["dest"] = pb.read_fixed32(val)
        elif field == 5:
            d["source"] = pb.read_fixed32(val)
        elif field == 6:
            d["request_id"] = pb.read_fixed32(val)
        elif field == 7:
            d["reply_id"] = pb.read_fixed32(val)
        elif field == 8:
            d["emoji"] = pb.read_fixed32(val)
    return d


# --- Position -----------------------------------------------------------
def encode_position(lat_deg, lon_deg, alt_m=0, ts=0, sats=0, precision_bits=0,
                    location_source=LOC_UNSET, ground_speed=None, ground_track=None):
    lat_i = int(round(lat_deg * 1e7))
    lon_i = int(round(lon_deg * 1e7))
    out = pb.field_fixed32(1, lat_i, signed=True) + pb.field_fixed32(2, lon_i, signed=True)
    if alt_m:
        out += pb.field_varint(3, int(alt_m))
    if ts:
        out += pb.field_fixed32(4, int(ts))
    if location_source:
        out += pb.field_varint(5, location_source)
    if ground_speed is not None:
        out += pb.field_varint(15, int(ground_speed))
    if ground_track is not None:
        out += pb.field_varint(16, int(ground_track))
    if sats:
        out += pb.field_varint(19, int(sats))
    if precision_bits:
        out += pb.field_varint(23, int(precision_bits))
    return out


def decode_position(buf):
    d = {"lat": None, "lon": None, "alt": 0, "time": 0, "sats": 0,
         "precision_bits": 0, "location_source": 0,
         "ground_speed": None, "ground_track": None}
    for field, wire, val in pb.iter_fields(buf):
        if field == 1:
            d["lat"] = pb.read_fixed32(val, signed=True) / 1e7
        elif field == 2:
            d["lon"] = pb.read_fixed32(val, signed=True) / 1e7
        elif field == 3:
            d["alt"] = pb.to_signed32(val)
        elif field == 4:
            d["time"] = pb.read_fixed32(val)
        elif field == 5:
            d["location_source"] = val
        elif field == 15:
            d["ground_speed"] = val
        elif field == 16:
            d["ground_track"] = val
        elif field == 19:
            d["sats"] = val
        elif field == 23:
            d["precision_bits"] = val
    return d


# --- User / NodeInfo ----------------------------------------------------
def encode_user(node_id, long_name, short_name, hw_model=0, role=0, is_licensed=False):
    out = pb.field_string(1, node_id) + pb.field_string(2, long_name) + pb.field_string(3, short_name)
    if is_licensed:
        out += pb.field_varint(6, 1)
    if hw_model:
        out += pb.field_varint(5, hw_model)
    if role:
        out += pb.field_varint(7, role)
    return out


def decode_user(buf):
    d = {"id": "", "long_name": "", "short_name": "", "hw_model": 0,
         "role": 0, "is_licensed": False}
    for field, wire, val in pb.iter_fields(buf):
        if field == 1:
            d["id"] = val.decode("utf-8", "replace") if isinstance(val, (bytes, bytearray)) else val
        elif field == 2:
            d["long_name"] = val.decode("utf-8", "replace")
        elif field == 3:
            d["short_name"] = val.decode("utf-8", "replace")
        elif field == 5:
            d["hw_model"] = val
        elif field == 6:
            d["is_licensed"] = bool(val)
        elif field == 7:
            d["role"] = val
    return d
