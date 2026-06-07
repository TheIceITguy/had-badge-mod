"""Time service: sets the RTC from GPS UTC (EV_TIME_SYNC) once a fix is acquired."""
from machine import RTC

from core.services import Service
from core.events import EV_TIME_SYNC


class TimeService(Service):
    name = "time"

    def __init__(self, badge):
        super().__init__(badge)
        self.rtc = RTC()
        self.synced = False

    def start(self):
        super().start()
        self.events.subscribe(EV_TIME_SYNC, self._on_time)

    def _on_time(self, dt):
        # dt: (year, month, day, weekday, hh, mm, ss, subsec)
        if not dt:
            return
        try:
            self.rtc.datetime(dt)
            self.synced = True
            print("RTC set from GPS:", dt)
        except Exception as exc:  # noqa: BLE001
            print("time set failed:", exc)

    def status(self):
        return {"name": self.name, "synced": self.synced}
