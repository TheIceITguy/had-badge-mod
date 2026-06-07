"""Mesh node database + automatic beaconing.

Tracks heard nodes (name, last position, SNR, last-heard) from received messages,
periodically broadcasts our NodeInfo, and — when enabled and a GPS fix is present
— shares our position over the mesh. Publishes EV_MESH_NODE_UPDATE so the node-map
app can refresh. Works with whichever backend is active (positions/nodeinfo are
only produced by the Meshtastic backend).
"""
import time

try:
    import asyncio as aio
except ImportError:  # pragma: no cover
    import uasyncio as aio

from core.services import Service
from core.events import EV_MESSAGE_RECEIVED, EV_MESH_NODE_UPDATE
from core.settings import Setting, TYPE_INT
from net.backend import KIND_TEXT, KIND_POSITION, KIND_NODEINFO


class NodeRecord:
    def __init__(self, num):
        self.num = num
        self.long_name = None
        self.short_name = None
        self.lat = None
        self.lon = None
        self.alt = 0
        self.snr = None
        self.rssi = None
        self.last_heard = 0

    def name(self):
        return self.long_name or self.short_name or ("!%08x" % self.num)

    def has_position(self):
        return self.lat is not None and self.lon is not None


class MeshService(Service):
    name = "mesh"

    def __init__(self, badge):
        super().__init__(badge)
        if self.settings is not None:
            self.settings.register(
                Setting("mesh_position_interval_s", TYPE_INT, 60,
                        "Position share interval (s)", "Radio", minv=10, maxv=3600))
        self._nodes = {}
        self._task = None
        self._last_pos_share = 0
        self._last_nodeinfo = 0

    def start(self):
        super().start()
        self.events.subscribe(EV_MESSAGE_RECEIVED, self._on_message)
        if self._task is None:
            self._task = aio.create_task(self._beacon_loop())

    # --- node database --------------------------------------------------
    def _node(self, num):
        rec = self._nodes.get(num)
        if rec is None:
            rec = NodeRecord(num)
            self._nodes[num] = rec
        return rec

    def _on_message(self, msg):
        if msg is None or msg.from_id is None:
            return
        rec = self._node(msg.from_id)
        rec.last_heard = int(time.time())
        if msg.snr is not None:
            rec.snr = msg.snr
        if msg.rssi is not None:
            rec.rssi = msg.rssi
        if msg.kind == KIND_NODEINFO:
            rec.long_name = msg.long_name or rec.long_name
            rec.short_name = msg.short_name or rec.short_name
        elif msg.kind == KIND_POSITION:
            rec.lat = msg.lat
            rec.lon = msg.lon
            rec.alt = msg.alt or 0
        self.events.publish(EV_MESH_NODE_UPDATE, rec)

    def nodes(self):
        return list(self._nodes.values())

    def node(self, num):
        return self._nodes.get(num)

    def count(self):
        return len(self._nodes)

    def share_position(self, enable):
        if self.settings is not None:
            self.settings.set("mesh_share_position", bool(enable))

    def status(self):
        return {"name": self.name, "nodes": len(self._nodes)}

    # --- periodic beacon ------------------------------------------------
    def _share_enabled(self):
        return bool(self.settings.get("mesh_share_position", False)) if self.settings else False

    async def _beacon_loop(self):
        while True:
            await aio.sleep(30)
            router = getattr(self.badge, "net_router", None)
            if router is None:
                continue
            now = int(time.time())
            if now - self._last_nodeinfo > 300:  # NodeInfo every ~5 min
                try:
                    router.send_nodeinfo()
                except Exception as exc:  # noqa: BLE001
                    print("mesh nodeinfo:", exc)
                self._last_nodeinfo = now
            if self._share_enabled():
                gps = self.badge.services.get("gps")
                fix = gps.fix() if gps else None
                try:
                    interval = int(self.settings.get("mesh_position_interval_s", 60))
                except (TypeError, ValueError):
                    interval = 60
                if fix and fix.get("valid") and now - self._last_pos_share >= interval:
                    try:
                        router.send_position(fix["lat"], fix["lon"], fix.get("alt", 0), fix.get("ts", 0))
                    except Exception as exc:  # noqa: BLE001
                        print("mesh position:", exc)
                    self._last_pos_share = now
