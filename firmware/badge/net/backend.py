"""Backend-agnostic messaging layer.

Apps send/receive logical messages (text, position, node info) without caring
which LoRa stack is active. Only ONE backend is active at a time because there is
a single radio. The MessageRouter owns the active backend, bridges incoming
messages onto the EventBus, and is reachable as ``badge.net_router`` and as the
``net`` service.
"""
import sys

from core.services import Service
from core.events import EV_MESSAGE_RECEIVED, EV_MESSAGE_SENT, EV_BACKEND_CHANGED

BROADCAST = 0xFFFFFFFF

KIND_TEXT = "text"
KIND_POSITION = "position"
KIND_NODEINFO = "nodeinfo"


class Message:
    """A logical message handed to/from apps, independent of the wire format."""

    def __init__(self, kind, from_id=None, from_name=None, to_id=BROADCAST, channel=0,
                 text=None, lat=None, lon=None, alt=None, ts=None, sats=None,
                 long_name=None, short_name=None, hw=None, snr=None, rssi=None, raw=None):
        self.kind = kind
        self.from_id = from_id
        self.from_name = from_name
        self.to_id = to_id
        self.channel = channel
        self.text = text
        self.lat = lat
        self.lon = lon
        self.alt = alt
        self.ts = ts
        self.sats = sats
        self.long_name = long_name
        self.short_name = short_name
        self.hw = hw
        self.snr = snr
        self.rssi = rssi
        self.raw = raw

    @classmethod
    def text_msg(cls, text, from_id=None, from_name=None, channel=0, to_id=BROADCAST):
        return cls(KIND_TEXT, from_id=from_id, from_name=from_name, text=text,
                   channel=channel, to_id=to_id)

    @classmethod
    def position_msg(cls, lat, lon, alt=0, ts=0, sats=0, from_id=None, from_name=None):
        return cls(KIND_POSITION, from_id=from_id, from_name=from_name,
                   lat=lat, lon=lon, alt=alt, ts=ts, sats=sats)

    @classmethod
    def nodeinfo_msg(cls, from_id, long_name, short_name, hw=0):
        return cls(KIND_NODEINFO, from_id=from_id, long_name=long_name,
                   short_name=short_name, hw=hw)

    def __repr__(self):
        if self.kind == KIND_TEXT:
            return "Message(text, from=%s, ch=%s, %r)" % (self.from_id, self.channel, self.text)
        if self.kind == KIND_POSITION:
            return "Message(position, from=%s, %s,%s)" % (self.from_id, self.lat, self.lon)
        return "Message(%s, from=%s)" % (self.kind, self.from_id)


class NetBackend:
    """One LoRa messaging stack. Subclasses: BadgeNetBackend, MeshtasticBackend."""

    name = "backend"

    def __init__(self, badge):
        self.badge = badge
        self._callbacks = []

    def on_message(self, callback):
        """Register a fast, non-async, no-LVGL callback(Message) for RX."""
        self._callbacks.append(callback)

    def _emit(self, message):
        for cb in self._callbacks:
            try:
                cb(message)
            except Exception as exc:  # noqa: BLE001
                print("NetBackend %s: rx callback error:" % self.name)
                if hasattr(sys, "print_exception"):
                    sys.print_exception(exc)
                else:
                    print(exc)

    # Subclasses override the following.
    def activate(self):
        """Reconfigure the radio for this backend and start receiving."""

    def deactivate(self):
        """Stop receiving / free resources before another backend activates."""

    def send_text(self, text, channel=0, to_id=BROADCAST):
        raise NotImplementedError

    def send_position(self, lat, lon, alt=0, ts=0):
        return False

    def send_nodeinfo(self):
        return False

    def my_node_id(self):
        return 0

    def status(self):
        return {"name": self.name}


class MessageRouter(Service):
    name = "net"

    def __init__(self, badge):
        super().__init__(badge)
        self.backends = {}
        self.active = None
        self.active_name = None

    def register_backend(self, backend):
        self.backends[backend.name] = backend
        backend.on_message(self._on_message)
        return backend

    def available(self):
        return list(self.backends.keys())

    def _on_message(self, message):
        # Fast path from the radio RX task: just fan out on the event bus.
        self.events.publish(EV_MESSAGE_RECEIVED, message)

    def set_backend(self, name):
        if name not in self.backends:
            print("net: backend %r unavailable; available=%s" % (name, self.available()))
            if not self.backends:
                return
            name = "badgenet" if "badgenet" in self.backends else self.available()[0]
        if self.active_name == name:
            return
        if self.active is not None:
            try:
                self.active.deactivate()
            except Exception as exc:  # noqa: BLE001
                print("net: deactivate %r failed: %s" % (self.active_name, exc))
        self.active = self.backends[name]
        self.active_name = name
        try:
            self.active.activate()
        except Exception as exc:  # noqa: BLE001
            print("net: activate %r failed:" % name)
            if hasattr(sys, "print_exception"):
                sys.print_exception(exc)
        # Persist the choice and announce it.
        try:
            self.badge.settings.set("net_backend", name)
        except Exception:  # noqa: BLE001
            pass
        self.events.publish(EV_BACKEND_CHANGED, name)

    # Backend-agnostic send API used by apps -----------------------------
    def send_text(self, text, channel=0, to_id=BROADCAST):
        if self.active is None:
            return False
        result = self.active.send_text(text, channel, to_id)
        self.events.publish(EV_MESSAGE_SENT,
                            Message.text_msg(text, from_id=self.active.my_node_id(),
                                             channel=channel, to_id=to_id))
        return result

    def send_position(self, lat, lon, alt=0, ts=0):
        return self.active.send_position(lat, lon, alt, ts) if self.active else False

    def send_nodeinfo(self):
        return self.active.send_nodeinfo() if self.active else False

    def my_node_id(self):
        return self.active.my_node_id() if self.active else 0

    def status(self):
        return {"active": self.active_name, "available": self.available()}
