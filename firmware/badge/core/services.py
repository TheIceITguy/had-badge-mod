"""Service registry: named, long-lived, cross-cutting features on the badge.

A Service owns hardware/state (gps, mesh net, time, wifi, battery, ...) and
publishes events. Apps consume services via ``badge.services.get("gps")`` instead
of re-initialising hardware; if a service is absent they degrade gracefully.
Services are started once at boot by ``start_all()`` and may create their own
asyncio tasks (the event loop is already running when the Badge is constructed).
"""

import sys


class Service:
    name = "service"

    def __init__(self, badge):
        self.badge = badge
        self.events = badge.events
        self.settings = getattr(badge, "settings", None)
        self._started = False

    def start(self):
        """Override: create tasks, subscribe to events, register settings."""
        self._started = True

    def stop(self):
        self._started = False

    def status(self):
        """Override: return a small dict for the settings/WebUI introspection."""
        return {"name": self.name, "started": self._started}


class ServiceRegistry:
    def __init__(self, badge):
        self.badge = badge
        self._services = {}

    def register(self, service):
        self._services[service.name] = service
        return service

    def get(self, name, default=None):
        return self._services.get(name, default)

    def has(self, name):
        return name in self._services

    def all(self):
        return list(self._services.values())

    def start_all(self):
        for service in self._services.values():
            try:
                service.start()
            except Exception as exc:  # noqa: BLE001 - a failing service must not block boot
                print("ServiceRegistry: failed to start '%s':" % service.name)
                if hasattr(sys, "print_exception"):
                    sys.print_exception(exc)
                else:
                    print(exc)
