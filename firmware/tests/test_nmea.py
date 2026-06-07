"""Host tests for the NMEA parser."""
from services.nmea import NmeaParser, parse_sentence, datetime_tuple, _dm_to_deg, _to_unix

RMC = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A"
GGA = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"


def test_rmc_fields():
    s = parse_sentence(RMC[1:])
    assert s["type"] == "RMC" and s["valid"]
    assert abs(s["lat"] - 48.1173) < 1e-3
    assert abs(s["lon"] - 11.51667) < 1e-3
    # NMEA 2-digit year is interpreted as 2000+yy (real fixes are 20xx); this
    # classic example's "94" therefore reads as 2094, not 1994.
    assert s["datetime"] == (2094, 3, 23, 0, 12, 35, 19, 0)
    assert s["ts"] > 0


def test_gga_fields():
    s = parse_sentence(GGA[1:])
    assert s["type"] == "GGA" and s["valid"]
    assert s["alt"] == 545
    assert s["sats"] == 8


def test_to_unix_known_epoch():
    assert _to_unix("010124", "000000") == 1704067200  # 2024-01-01T00:00:00Z


def test_bad_checksum_rejected():
    bad = "GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*00"
    assert parse_sentence(bad) is None


def test_dm_to_deg_hemisphere_sign():
    assert _dm_to_deg("4807.038", "S") < 0
    assert _dm_to_deg("01131.000", "W") < 0
    assert _dm_to_deg("", "N") is None


def test_feed_streaming_across_chunks():
    p = NmeaParser()
    full = RMC + "\r\n" + GGA + "\r\n"
    assert p.feed(full[:20]) == []           # partial line, nothing yet
    out = p.feed(full[20:])
    assert [s["type"] for s in out] == ["RMC", "GGA"]
