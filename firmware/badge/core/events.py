"""A tiny synchronous publish/subscribe event bus.

Handlers run on the publisher's stack, in subscription order. Because publishers
include the LoRa RX path (the network receive callbacks, which must be fast,
non-async, and must not touch LVGL), handlers MUST follow the same contract:
keep it short, do not ``await``, do not touch the display. For UI work, a handler
should only set a flag or append to a list; the owning app then renders in its
own ``run_foreground``. An exception in one handler is caught and printed; it
never stops the other handlers or the publisher.
"""

import sys

# Event names (payload shapes documented inline).
EV_POSITION_UPDATE = "position_update"      # dict(lat, lon, alt, ts, sats, source)
EV_MESSAGE_RECEIVED = "message_received"    # net.backend.Message
EV_MESSAGE_SENT = "message_sent"            # net.backend.Message
EV_MESH_NODE_UPDATE = "mesh_node_update"    # NodeRecord
EV_WIFI_STATE = "wifi_state"                # dict(mode, ip, ssid, connected)
EV_BATTERY = "battery"                      # dict(volts, pct, charging)
EV_BATTERY_LOW = "battery_low"             # dict(pct)
EV_TIME_SYNC = "time_sync"                  # epoch seconds (int)
EV_BACKEND_CHANGED = "backend_changed"      # backend name (str)
EV_MESSAGES_READ = "messages_read"          # Messages app viewed -> clear notify
EV_KEY = "key"                              # str (one key) - opt-in


class EventBus:
    def __init__(self):
        self._subs = {}

    def subscribe(self, event_name, handler):
        self._subs.setdefault(event_name, []).append(handler)

    def unsubscribe(self, event_name, handler):
        handlers = self._subs.get(event_name)
        if handlers:
            try:
                handlers.remove(handler)
            except ValueError:
                pass

    def publish(self, event_name, payload=None):
        for handler in self._subs.get(event_name, ()):
            try:
                handler(payload)
            except Exception as exc:  # noqa: BLE001 - one bad handler must not break others
                print("EventBus: handler for '%s' raised:" % event_name)
                sys.print_exception(exc) if hasattr(sys, "print_exception") else print(exc)

    def subscriber_count(self, event_name):
        return len(self._subs.get(event_name, ()))
