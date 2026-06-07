"""Meshtastic region frequency plans, modem presets, and slot math.

freqStart/freqEnd are in MHz, from Meshtastic firmware RegionInfo. Channel slot:
  numChannels = floor((end - start) / (bw_kHz/1000))
  slot        = djb2(channel_name) % numChannels      (0-based)
  center      = start + bw_kHz/2000 + slot*(bw_kHz/1000)
Verified KATs: EU_868 LongFast -> 869.525; US LongFast slot 19 -> 906.875.

NOTE: only EU_868 and US are KAT-verified here. Other regions use widely
documented Meshtastic values; confirm against current firmware before relying on
them for legal transmission in your jurisdiction.
"""

SYNC_WORD = 0x2B  # Meshtastic LoRa sync word

# name -> (freqStart, freqEnd, power_limit_dBm)
REGIONS = {
    "US": (902.0, 928.0, 30),
    "EU_868": (869.4, 869.65, 16),
    "EU_433": (433.0, 434.0, 12),
    "ANZ": (915.0, 928.0, 30),
    "AU_915": (915.0, 928.0, 30),
    "IN": (865.0, 867.0, 30),
    "JP": (920.8, 927.8, 16),
    "KR": (920.0, 923.0, 0),
    "CN": (470.0, 510.0, 19),
    "RU": (868.7, 869.2, 20),
    "TW": (920.0, 925.0, 27),
    "TH": (920.0, 925.0, 16),
    "UA_868": (868.0, 868.6, 14),
}

# preset name -> (bandwidth_kHz, spreading_factor, coding_rate, preamble)
# coding_rate is the SX126x value (5..8 == 4/5..4/8). preamble fixed at 16.
PRESETS = {
    "ShortTurbo": (500.0, 7, 5, 16),
    "ShortFast": (250.0, 7, 5, 16),
    "ShortSlow": (250.0, 8, 5, 16),
    "MediumFast": (250.0, 9, 5, 16),
    "MediumSlow": (250.0, 10, 5, 16),
    "LongFast": (250.0, 11, 5, 16),
    "LongModerate": (125.0, 11, 8, 16),
    "LongSlow": (125.0, 12, 8, 16),
    "VeryLongSlow": (62.5, 12, 8, 16),
}

DEFAULT_REGION = "EU_868"
DEFAULT_PRESET = "LongFast"


def preset_params(preset):
    """Return (bw_kHz, sf, cr, preamble) for a preset name."""
    return PRESETS[preset]


def djb2(name):
    if isinstance(name, str):
        name = name.encode("utf-8")
    h = 5381
    for c in name:
        h = ((h << 5) + h + c) & 0xFFFFFFFF
    return h


def num_channels(region, bw_khz):
    start, end, _ = REGIONS[region]
    spacing = bw_khz / 1000.0  # MHz
    return int((end - start) / spacing + 1e-6)


def slot_for_channel(channel_name, region, bw_khz):
    n = num_channels(region, bw_khz)
    if n <= 0:
        return 0
    return djb2(channel_name) % n


def center_freq(channel_name, region, bw_khz):
    start, _, _ = REGIONS[region]
    slot = slot_for_channel(channel_name, region, bw_khz)
    return start + (bw_khz / 2000.0) + slot * (bw_khz / 1000.0)


def power_limit(region):
    return REGIONS[region][2]
