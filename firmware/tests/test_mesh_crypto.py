"""Host tests for Meshtastic channel crypto."""
import os

import pytest

from net.mesh import mesh_crypto as mc


def test_expand_default_psk():
    assert mc.expand_psk(b"\x01") == mc.DEFAULT_PSK
    assert mc.parse_psk("AQ==") == mc.DEFAULT_PSK  # base64 of 0x01


def test_expand_shorthand_index():
    k2 = mc.expand_psk(b"\x02")
    assert k2[:15] == mc.DEFAULT_PSK[:15]
    assert k2[15] == (mc.DEFAULT_PSK[15] + 1) & 0xFF


def test_expand_disabled_and_full_keys():
    assert mc.expand_psk(b"\x00") == b""
    assert mc.expand_psk(b"") == b""
    full = bytes(range(16))
    assert mc.expand_psk(full) == full
    with pytest.raises(ValueError):
        mc.expand_psk(b"123")  # invalid length


def test_channel_hash_default_longfast_is_0x08():
    # The canonical Meshtastic default channel hash.
    assert mc.channel_hash("LongFast", b"\x01") == 0x08
    assert mc.channel_hash("LongFast", mc.DEFAULT_PSK) == 0x08


def test_nonce_layout():
    nonce = mc.build_nonce(0x11223344, 0xAABBCCDD)
    assert nonce[0:4] == b"\x44\x33\x22\x11"   # packet id u32 LE
    assert nonce[4:8] == b"\x00\x00\x00\x00"   # high 32 of u64 pid
    assert nonce[8:12] == b"\xdd\xcc\xbb\xaa"  # from node u32 LE
    assert nonce[12:16] == b"\x00\x00\x00\x00"


def test_aes_ctr_roundtrip():
    key = mc.DEFAULT_PSK
    nonce = mc.build_nonce(0xDEADBEEF, 0x12345678)
    pt = b"Hello Meshtastic, this is a badge!"
    ct = mc.aes_ctr_xcrypt(key, nonce, pt)
    assert ct != pt
    assert mc.aes_ctr_xcrypt(key, nonce, ct) == pt


def test_native_and_ecb_fallback_match():
    """The ECB-built CTR must equal the native CTR for arbitrary inputs — this is
    what guarantees correctness on a device that lacks native CTR."""
    if not mc.HAVE_NATIVE_CTR:
        pytest.skip("native CTR unavailable on host")
    for _ in range(20):
        key = os.urandom(16)
        nonce = os.urandom(16)
        data = os.urandom(int.from_bytes(os.urandom(1), "big") or 1)
        assert mc._aes_ctr_native(key, nonce, data) == mc._ctr_from_ecb(key, nonce, data)


def test_aes256_key():
    key = os.urandom(32)
    nonce = mc.build_nonce(1, 2)
    pt = b"x" * 40
    assert mc.aes_ctr_xcrypt(key, nonce, mc.aes_ctr_xcrypt(key, nonce, pt)) == pt
