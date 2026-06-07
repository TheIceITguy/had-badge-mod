"""Host tests for region/preset/frequency-slot math."""
from net.mesh import regions as r


def test_djb2_known():
    assert r.djb2("LongFast") == 130429955


def test_num_channels():
    assert r.num_channels("US", 250.0) == 104
    assert r.num_channels("EU_868", 250.0) == 1


def test_eu868_longfast_center():
    assert abs(r.center_freq("LongFast", "EU_868", 250.0) - 869.525) < 1e-6


def test_us_longfast_slot_and_center():
    assert r.slot_for_channel("LongFast", "US", 250.0) == 19
    assert abs(r.center_freq("LongFast", "US", 250.0) - 906.875) < 1e-6


def test_preset_params():
    bw, sf, cr, pre = r.preset_params("LongFast")
    assert (bw, sf, cr, pre) == (250.0, 11, 5, 16)
    assert r.SYNC_WORD == 0x2B


def test_defaults():
    assert r.DEFAULT_REGION == "EU_868"
    assert r.DEFAULT_PRESET == "LongFast"
