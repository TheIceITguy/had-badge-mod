"""Meshtastic protocol stack for the Communicator Badge.

Pure-Python, dual-runtime (CPython on the host for tests, MicroPython on the badge).
Modules:
  protobuf    - minimal protobuf wire codec
  pb_messages - encode/decode the Data / Position / User messages we use
  mesh_crypto - default PSK, channel hash, AES-CTR nonce, AES-CTR encrypt/decrypt
  packet      - 16-byte MeshPacket header + full packet build/parse
  regions     - region/preset tables + frequency-slot math

Wire details pinned to the Meshtastic protobufs/firmware; see net/mesh/SPEC.md.
"""
