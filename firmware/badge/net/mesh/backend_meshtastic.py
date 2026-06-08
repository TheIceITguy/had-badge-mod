"""Meshtastic backend: makes the badge a real node on a Meshtastic LoRa mesh.

Reconfigures the SX1262 to the selected region + modem preset, then runs its own
RX/TX tasks (BadgeNet is paused while this backend is active — single radio). It
builds/parses real Meshtastic packets (see net/mesh/) and does a conservative
flood rebroadcast of broadcast packets (hop_limit-1) with (from,id) de-dup.

Pure, host-testable helpers (_build_*_frame, _handle_frame, decrement_hop) carry
the logic; the async loops are thin wrappers around them.
"""
import time
from collections import deque

try:
    import asyncio as aio
except ImportError:  # pragma: no cover
    import uasyncio as aio

from net.backend import NetBackend, Message, BROADCAST, KIND_TEXT, KIND_POSITION, KIND_NODEINFO
from net.net import badgenet
from net.mesh import regions
from net.mesh.mesh_crypto import parse_psk
from net.mesh.packet import build_packet, parse_packet, gen_packet_id
from net.mesh import pb_messages as pbm
from core.settings import Setting, TYPE_INT, TYPE_BOOL, TYPE_ENUM, TYPE_STR


def register_meshtastic_settings(settings):
    settings.register_many([
        Setting("net_backend", TYPE_ENUM, "meshtastic", "Network stack", "Network",
                choices=["meshtastic", "badgenet"],
                help="Meshtastic = interop with real Meshtastic devices. "
                     "BadgeNet = stock badge-to-badge chat."),
        Setting("mesh_region", TYPE_ENUM, regions.DEFAULT_REGION, "LoRa region", "Radio",
                choices=sorted(regions.REGIONS.keys()),
                help="Must match your locale's legal band and the other nodes."),
        Setting("mesh_preset", TYPE_ENUM, regions.DEFAULT_PRESET, "Modem preset", "Radio",
                choices=list(regions.PRESETS.keys())),
        Setting("mesh_channel_name", TYPE_STR, "LongFast", "Channel name", "Radio"),
        Setting("mesh_psk", TYPE_STR, "AQ==", "Channel key (base64)", "Radio",
                help="'AQ==' is the public default channel key."),
        Setting("mesh_hop_limit", TYPE_INT, 3, "Hop limit", "Radio", minv=0, maxv=7),
        Setting("mesh_share_position", TYPE_BOOL, False, "Share my GPS position", "Radio"),
        Setting("mesh_rebroadcast", TYPE_BOOL, False, "Relay others' packets", "Radio",
                help="Off = handheld client (don't retransmit). On = act as a router."),
    ])


class MeshtasticBackend(NetBackend):
    name = "meshtastic"

    def __init__(self, badge):
        super().__init__(badge)
        if getattr(badge, "settings", None) is not None:
            register_meshtastic_settings(badge.settings)
        self.my_node = getattr(badge, "node_id", 0)
        self._tx_queue = deque((), 24)
        self._rx_task = None
        self._tx_task = None
        self._seen = {}
        self._load_config()

    # --- configuration --------------------------------------------------
    def _setting(self, key, default):
        s = getattr(self.badge, "settings", None)
        return s.get(key, default) if s is not None else default

    def _load_config(self):
        self.region = self._setting("mesh_region", regions.DEFAULT_REGION)
        self.preset = self._setting("mesh_preset", regions.DEFAULT_PRESET)
        self.channel_name = self._setting("mesh_channel_name", "LongFast")
        self.psk = parse_psk(self._setting("mesh_psk", "AQ=="))
        self.hop_limit = int(self._setting("mesh_hop_limit", 3))
        self.rebroadcast = bool(self._setting("mesh_rebroadcast", False))
        try:
            self.tx_power = int(self._setting("radio_tx_power", 9))
        except (TypeError, ValueError):
            self.tx_power = 9
        self.bw, self.sf, self.cr, self.preamble = regions.preset_params(self.preset)
        self.frequency = regions.center_freq(self.channel_name, self.region, self.bw)

    def radio_params(self):
        return dict(freq=self.frequency, bw=self.bw, sf=self.sf, cr=self.cr,
                    sync=regions.SYNC_WORD, preamble=self.preamble, power=self.tx_power)

    # --- frame builders (pure) -----------------------------------------
    def _frame(self, portnum, payload, to_id=BROADCAST):
        data = pbm.encode_data(portnum, payload)
        return build_packet(to_id, self.my_node, gen_packet_id(),
                            self.channel_name, self.psk, data,
                            hop_limit=self.hop_limit, hop_start=self.hop_limit)

    def _build_text_frame(self, text, to_id=BROADCAST):
        return self._frame(pbm.TEXT_MESSAGE_APP, text.encode("utf-8"), to_id)

    def _build_position_frame(self, lat, lon, alt=0, ts=0, sats=0):
        if not ts:
            ts = int(time.time())
        payload = pbm.encode_position(lat, lon, alt_m=alt, ts=ts, sats=sats)
        return self._frame(pbm.POSITION_APP, payload)

    def _build_nodeinfo_frame(self):
        node_id = "!%08x" % self.my_node
        long_name = self.badge.device_name() if hasattr(self.badge, "device_name") else node_id
        short_name = self._setting("short_name", long_name[:4])
        payload = pbm.encode_user(node_id, long_name, short_name)
        return self._frame(pbm.NODEINFO_APP, payload)

    @staticmethod
    def decrement_hop(frame, relay_node):
        """Return the frame with hop_limit-1 and relay_node set, or None if hops exhausted."""
        flags = frame[12]
        hop = flags & 0x07
        if hop == 0:
            return None
        out = bytearray(frame)
        out[12] = (flags & 0xF8) | (hop - 1)
        out[15] = relay_node & 0xFF
        return bytes(out)

    # --- RX handling (pure) --------------------------------------------
    def _data_to_message(self, hdr, data):
        try:
            snr = self.badge.lora.get_snr()
            rssi = self.badge.lora.get_rssi()
        except Exception:  # noqa: BLE001
            snr = rssi = None
        port = data["portnum"]
        if port == pbm.TEXT_MESSAGE_APP:
            return Message(KIND_TEXT, from_id=hdr["from"],
                           text=data["payload"].decode("utf-8", "replace"),
                           channel=0, snr=snr, rssi=rssi)
        if port == pbm.POSITION_APP:
            p = pbm.decode_position(data["payload"])
            return Message(KIND_POSITION, from_id=hdr["from"], lat=p["lat"], lon=p["lon"],
                           alt=p["alt"], ts=p["time"], sats=p["sats"], snr=snr, rssi=rssi)
        if port == pbm.NODEINFO_APP:
            u = pbm.decode_user(data["payload"])
            return Message(KIND_NODEINFO, from_id=hdr["from"], long_name=u["long_name"],
                           short_name=u["short_name"], hw=u["hw_model"], snr=snr, rssi=rssi)
        return None

    def _handle_frame(self, frame, now=None):
        """Parse a received frame. Returns (Message|None, rebroadcast_frame|None)."""
        parsed = parse_packet(frame, self.channel_name, self.psk)
        if parsed is None:
            return None, None
        hdr, data = parsed
        key = (hdr["from"], hdr["id"])
        if key in self._seen:
            return None, None
        if len(self._seen) > 400:
            self._seen.clear()
        self._seen[key] = now if now is not None else 0
        if hdr["from"] == self.my_node:
            return None, None  # our own packet echoed back
        rebroadcast = None
        if self.rebroadcast and hdr["to"] == BROADCAST and hdr["hop_limit"] > 0:
            rebroadcast = self.decrement_hop(frame, self.my_node)
        return self._data_to_message(hdr, data), rebroadcast

    # --- NetBackend API ------------------------------------------------
    def activate(self):
        self._load_config()
        # Pause BadgeNet so only this backend consumes the radio.
        try:
            badgenet.stop()
        except Exception:  # noqa: BLE001
            pass
        if hasattr(self.badge.lora, "reconfigure"):
            self.badge.lora.reconfigure(**self.radio_params())
        self._seen.clear()
        if self._rx_task is None:
            self._rx_task = aio.create_task(self._rx_loop())
        if self._tx_task is None:
            self._tx_task = aio.create_task(self._tx_loop())
        # Announce ourselves on the mesh.
        self._enqueue(self._build_nodeinfo_frame())

    def deactivate(self):
        for task in (self._rx_task, self._tx_task):
            try:
                if task:
                    task.cancel()
            except Exception:  # noqa: BLE001
                pass
        self._rx_task = None
        self._tx_task = None

    def _enqueue(self, frame):
        if frame is not None:
            self._tx_queue.append(frame)

    def send_text(self, text, channel=0, to_id=BROADCAST):
        self._enqueue(self._build_text_frame(text, to_id))
        return True

    def send_position(self, lat, lon, alt=0, ts=0):
        self._enqueue(self._build_position_frame(lat, lon, alt, ts))
        return True

    def send_nodeinfo(self):
        self._enqueue(self._build_nodeinfo_frame())
        return True

    def my_node_id(self):
        return self.my_node

    def status(self):
        return {"name": self.name, "node": "!%08x" % self.my_node,
                "region": self.region, "preset": self.preset,
                "freq": self.frequency, "channel": self.channel_name}

    # --- async loops (thin) --------------------------------------------
    async def _rx_loop(self):
        while True:
            try:
                frame = await self.badge.lora.recv()
                if frame:
                    message, rebroadcast = self._handle_frame(frame, now=int(time.time()))
                    if rebroadcast is not None:
                        self._enqueue(rebroadcast)
                    if message is not None:
                        self._emit(message)
            except Exception as exc:  # noqa: BLE001
                print("meshtastic rx:", exc)
            await aio.sleep_ms(2)

    async def _tx_loop(self):
        while True:
            if self._tx_queue:
                frame = self._tx_queue.popleft()
                try:
                    await self.badge.lora.send(frame)
                except Exception as exc:  # noqa: BLE001
                    print("meshtastic tx:", exc)
                await aio.sleep_ms(50)
            else:
                await aio.sleep_ms(50)
