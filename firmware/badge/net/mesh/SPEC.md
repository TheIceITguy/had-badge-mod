# Meshtastic wire protocol — pinned facts

This stack targets the Meshtastic LoRa protocol as of **firmware 2.x / protobufs
master (mid-2026)**. Re-validate the KATs in `firmware/tests/` against a current
device capture before any release, as the protocol can evolve.

## PHY (LongFast default)
- Modem preset LongFast: BW 250 kHz, SF 11, CR 4/5, preamble 16.
- LoRa sync word: `0x2B`.
- Region frequency = `start + bw_kHz/2000 + slot*(bw_kHz/1000)`, where
  `slot = djb2(channel_name) % numChannels`, `numChannels = floor((end-start)/(bw/1000))`.
  - EU_868 (869.4–869.65): 1 slot → **869.525 MHz**.
  - US (902.0–928.0): LongFast → slot **19** → **906.875 MHz**.

## Packet header (16 bytes, little-endian)
`to:u32 | from:u32 | id:u32 | flags:u8 | channel:u8 | next_hop:u8 | relay_node:u8`
- flags: `hop_limit = f&0x07`, `want_ack = f&0x08`, `via_mqtt = f&0x10`, `hop_start = (f&0xE0)>>5`.
- LongFast default TX: hop_limit=3, hop_start=3 → flags `0x63`; broadcast `to=0xFFFFFFFF`.
- `channel` = 1-byte decryption hint (see channel hash). `relay_node` = sender NodeNum & 0xFF.

## Payload
AES-CTR ciphertext of the protobuf `Data` message.
- Default PSK `AQ==` (1 byte `0x01`) expands to `d4f1bb3a 20290759 f0bcffab cf4e6901` (AES-128).
  Shorthand `n`: copy default, `key[15] += n-1`.
- Channel hash byte = `xorHash(name) ^ xorHash(expanded_psk)`. Default "LongFast" → **0x08**.
- AES-CTR nonce (16 B): `packetId` u64 LE (bytes 0–7) | `fromNode` u32 LE (8–11) | zeros (12–15).
  Standard CTR: nonce is the initial 128-bit big-endian counter, +1 per block.

## protobuf field numbers
- **Data**: portnum=1, payload=2, want_response=3, dest=4(fixed32), source=5(fixed32),
  request_id=6(fixed32), reply_id=7(fixed32), emoji=8(fixed32).
- **Position**: latitude_i=1(sfixed32, deg×1e7), longitude_i=2(sfixed32), altitude=3(int32),
  time=4(fixed32), location_source=5, ground_speed=15, ground_track=16(1/100°),
  sats_in_view=19, precision_bits=23.
- **User**: id=1(string), long_name=2, short_name=3, macaddr=4(bytes), hw_model=5, is_licensed=6, role=7.
- **PortNum**: TEXT=1, POSITION=3, NODEINFO=4, ROUTING=5, ADMIN=6, TELEMETRY=67, TRACEROUTE=70.

## Sources
meshtastic/protobufs `mesh.proto`, `portnums.proto`; meshtastic/firmware
`RadioInterface.{h,cpp}`, `Channels.cpp`, `CryptoEngine.cpp`; meshtastic.org radio-settings.
