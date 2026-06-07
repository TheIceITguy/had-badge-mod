"""Meshtastic channel crypto: default PSK, channel hash, AES-CTR nonce + cipher.

AES-CTR is taken from the `cryptography` (ucryptography) module when available
(works on host and on the badge build). If CTR is missing, a portable CTR is
built from AES-ECB (cryptography on host, ucryptolib on the badge). The two paths
are cross-checked in the tests so the fallback is provably equivalent.

Verified Meshtastic facts:
  default PSK "AQ==" (1-byte 0x01) -> d4f1bb3a20290759f0bcffabcf4e6901 (AES-128)
  shorthand n: copy default key, key[15] += n-1
  channel hash byte = xorHash(name) ^ xorHash(expanded_psk)
  CTR nonce (16B) = packetId u64 LE (bytes 0-7) | fromNode u32 LE (8-11) | zeros (12-15)
"""

DEFAULT_PSK = bytes((0xD4, 0xF1, 0xBB, 0x3A, 0x20, 0x29, 0x07, 0x59,
                     0xF0, 0xBC, 0xFF, 0xAB, 0xCF, 0x4E, 0x69, 0x01))


def expand_psk(psk_field):
    """Expand a stored PSK field to the actual key bytes.

    b'' / b'\\x00' -> b'' (no encryption). 1-byte shorthand n -> default key with
    key[15] += n-1. 16- or 32-byte fields are returned unchanged.
    """
    if psk_field is None:
        return b""
    psk_field = bytes(psk_field)
    n = len(psk_field)
    if n == 0:
        return b""
    if n == 1:
        idx = psk_field[0]
        if idx == 0:
            return b""
        key = bytearray(DEFAULT_PSK)
        key[15] = (key[15] + idx - 1) & 0xFF
        return bytes(key)
    if n in (16, 32):
        return psk_field
    raise ValueError("PSK must be 0, 1, 16 or 32 bytes, got %d" % n)


def parse_psk(value):
    """Accept a base64 string (e.g. 'AQ=='), a hex-ish bytes field, or raw bytes,
    and return the expanded key bytes."""
    if isinstance(value, str):
        import binascii
        try:
            value = binascii.a2b_base64(value)
        except Exception:  # noqa: BLE001
            value = value.encode("utf-8")
    return expand_psk(value)


def xor_hash(data):
    if isinstance(data, str):
        data = data.encode("utf-8")
    h = 0
    for b in data:
        h ^= b
    return h


def channel_hash(name, psk_field):
    """The 1-byte channel hash stored in the packet header (decryption hint)."""
    return xor_hash(name) ^ xor_hash(expand_psk(psk_field))


def build_nonce(packet_id, from_node):
    nonce = bytearray(16)
    nonce[0:4] = (packet_id & 0xFFFFFFFF).to_bytes(4, "little")
    # bytes 4..7 stay zero (high 32 bits of the u64 packet id)
    nonce[8:12] = (from_node & 0xFFFFFFFF).to_bytes(4, "little")
    # bytes 12..15 stay zero (extraNonce, only set for PKI packets)
    return bytes(nonce)


# --- AES-CTR ------------------------------------------------------------
try:
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

    def _have_ctr():
        try:
            Cipher(algorithms.AES(bytes(16)), modes.CTR(bytes(16)))
            return True
        except Exception:  # noqa: BLE001
            return False

    HAVE_NATIVE_CTR = _have_ctr()
except ImportError:  # pragma: no cover - device may lack the module path
    Cipher = algorithms = modes = None
    HAVE_NATIVE_CTR = False


def _aes_ctr_native(key, nonce, data):
    enc = Cipher(algorithms.AES(key), modes.CTR(nonce)).encryptor()
    return enc.update(data) + enc.finalize()


def _ecb_encryptor(key):
    """Return a function encrypting one 16-byte block with AES-ECB."""
    if algorithms is not None:
        def enc(block):
            e = Cipher(algorithms.AES(key), modes.ECB()).encryptor()
            return e.update(block) + e.finalize()
        return enc
    import ucryptolib  # type: ignore
    cipher = ucryptolib.aes(key, 1)  # mode 1 = ECB

    def enc(block):
        return cipher.encrypt(block)
    return enc


def _ctr_from_ecb(key, nonce, data):
    """Standard AES-CTR built on ECB: 16-byte nonce is the initial 128-bit
    big-endian counter, incremented by one per block."""
    enc = _ecb_encryptor(key)
    out = bytearray(len(data))
    counter = bytearray(nonce)
    off = 0
    n = len(data)
    while off < n:
        ks = enc(bytes(counter))
        chunk = data[off:off + 16]
        for i in range(len(chunk)):
            out[off + i] = chunk[i] ^ ks[i]
        off += 16
        # increment 128-bit big-endian counter
        for i in range(15, -1, -1):
            counter[i] = (counter[i] + 1) & 0xFF
            if counter[i]:
                break
    return bytes(out)


def aes_ctr_xcrypt(key, nonce, data):
    """Encrypt or decrypt (CTR is symmetric). Empty key => no encryption."""
    if not key:
        return bytes(data)
    if HAVE_NATIVE_CTR:
        return _aes_ctr_native(key, nonce, data)
    return _ctr_from_ecb(key, nonce, data)
