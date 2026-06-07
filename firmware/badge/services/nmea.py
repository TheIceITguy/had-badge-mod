"""Minimal NMEA 0183 parser for the ATGM336H (and most GPS/GNSS modules).

Handles RMC (position, speed, course, date/time) and GGA (altitude, satellites,
fix quality). Pure Python, dual-runtime; fed raw UART bytes, yields parsed dicts.
"""


def _checksum_ok(sentence):
    # sentence like "GPRMC,...*4F" (without leading '$'); verify XOR checksum.
    star = sentence.rfind("*")
    if star < 0:
        return False
    body = sentence[:star]
    try:
        want = int(sentence[star + 1:star + 3], 16)
    except ValueError:
        return False
    got = 0
    for ch in body:
        got ^= ord(ch)
    return got == want


def _dm_to_deg(value, hemi):
    """Convert NMEA ddmm.mmmm + hemisphere to signed decimal degrees."""
    if not value:
        return None
    dot = value.find(".")
    if dot < 3:
        return None
    deg = int(value[:dot - 2])
    minutes = float(value[dot - 2:])
    dec = deg + minutes / 60.0
    if hemi in ("S", "W"):
        dec = -dec
    return dec


def _to_unix(date_ddmmyy, time_hhmmss):
    """Convert RMC date+time to a UTC unix timestamp (no leap seconds)."""
    if not date_ddmmyy or not time_hhmmss or len(date_ddmmyy) < 6:
        return 0
    try:
        day = int(date_ddmmyy[0:2])
        month = int(date_ddmmyy[2:4])
        year = 2000 + int(date_ddmmyy[4:6])
        hh = int(time_hhmmss[0:2])
        mm = int(time_hhmmss[2:4])
        ss = int(float(time_hhmmss[4:]))
    except (ValueError, IndexError):
        return 0
    # Days since unix epoch (1970-01-01), civil-from-days algorithm.
    y = year - (1 if month <= 2 else 0)
    era = (y if y >= 0 else y - 399) // 400
    yoe = y - era * 400
    doy = (153 * ((month + (-3 if month > 2 else 9)) % 12) + 2) // 5 + (day - 1)
    doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
    days = era * 146097 + doe - 719468
    return days * 86400 + hh * 3600 + mm * 60 + ss


def datetime_tuple(date_ddmmyy, time_hhmmss):
    """Return an RTC-style (year, month, day, 0, hh, mm, ss, 0) tuple or None."""
    if not date_ddmmyy or not time_hhmmss or len(date_ddmmyy) < 6:
        return None
    try:
        return (2000 + int(date_ddmmyy[4:6]), int(date_ddmmyy[2:4]), int(date_ddmmyy[0:2]),
                0, int(time_hhmmss[0:2]), int(time_hhmmss[2:4]), int(float(time_hhmmss[4:])), 0)
    except (ValueError, IndexError):
        return None


def parse_sentence(sentence):
    """Parse one NMEA sentence (without '$'). Returns a dict or None."""
    if not _checksum_ok(sentence):
        return None
    star = sentence.rfind("*")
    fields = sentence[:star].split(",")
    talker_type = fields[0]
    kind = talker_type[2:] if len(talker_type) >= 5 else talker_type
    if kind == "RMC" and len(fields) >= 10:
        status = fields[2]
        lat = _dm_to_deg(fields[3], fields[4])
        lon = _dm_to_deg(fields[5], fields[6])
        try:
            speed = float(fields[7]) if fields[7] else 0.0  # knots
        except ValueError:
            speed = 0.0
        try:
            track = float(fields[8]) if fields[8] else None  # degrees true
        except ValueError:
            track = None
        return {
            "type": "RMC",
            "valid": status == "A" and lat is not None and lon is not None,
            "lat": lat, "lon": lon, "speed": speed, "track": track,
            "ts": _to_unix(fields[9], fields[1]),
            "datetime": datetime_tuple(fields[9], fields[1]),
        }
    if kind == "GGA" and len(fields) >= 10:
        lat = _dm_to_deg(fields[2], fields[3])
        lon = _dm_to_deg(fields[4], fields[5])
        try:
            quality = int(fields[6]) if fields[6] else 0
        except ValueError:
            quality = 0
        try:
            sats = int(fields[7]) if fields[7] else 0
        except ValueError:
            sats = 0
        try:
            alt = int(float(fields[9])) if fields[9] else 0
        except ValueError:
            alt = 0
        return {
            "type": "GGA",
            "valid": quality > 0 and lat is not None and lon is not None,
            "lat": lat, "lon": lon, "alt": alt, "sats": sats, "quality": quality,
        }
    return None


class NmeaParser:
    def __init__(self):
        self._buf = ""

    def feed(self, chunk):
        """Feed raw bytes/str; return a list of parsed sentence dicts."""
        if isinstance(chunk, (bytes, bytearray)):
            chunk = chunk.decode("ascii", "replace")
        self._buf += chunk
        out = []
        while True:
            nl = self._buf.find("\n")
            if nl < 0:
                break
            line = self._buf[:nl].strip("\r\n ")
            self._buf = self._buf[nl + 1:]
            if line.startswith("$"):
                parsed = parse_sentence(line[1:])
                if parsed:
                    out.append(parsed)
        if len(self._buf) > 200:  # guard against a stuck partial line
            self._buf = self._buf[-100:]
        return out
