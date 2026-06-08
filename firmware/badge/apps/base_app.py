"""Template app for badge applications. Copy this file and update to implement your own app."""

import gc
import uasyncio as aio  # type: ignore

from hardware.badge import Badge


class BaseApp:
    """Base class for apps.
    See apps/template.py for full descriptions on how to use these methods.
    """

    # All apps that have been started
    all_apps = []

    def __init__(self, name: str, badge):
        self.name: str = name
        self.badge: Badge = badge
        self.active_foreground = False  # Current active app on the badge UI
        self.active_background = True  # Running in the background
        self.foreground_sleep_ms = 100
        self.background_sleep_ms = 1000
        self.task = None

    def start(self):
        """Register the app with the system. Start it running in the background."""
        if self.task is None:
            # print(
            #     f"Starting task for {self.name}. Foreground: {self.active_foreground} Background: {self.active_background}"
            # )
            self.task = aio.create_task(self.run())
            self.all_apps.append(self)

    def stop(self):
        """Unregister the app from the system."""
        self.active_foreground = False
        self.active_background = False
        gc.collect()

    async def run(self):
        """Run the app's main loop.

        Each tick is wrapped so a single error (e.g. an unbound LVGL call) prints a
        traceback and drops the app to the background instead of silently killing
        its task. The launcher then re-foregrounds itself, keeping the badge usable.
        """
        while True:
            try:
                if self.active_foreground:
                    self.run_foreground()
                    await aio.sleep_ms(self.foreground_sleep_ms)
                    if self.badge.check_background_current_app():
                        self.switch_to_background()
                elif self.active_background:
                    self.run_background()
                    await aio.sleep_ms(self.background_sleep_ms)
                else:
                    self.stop()
            except Exception as exc:  # noqa: BLE001
                import sys
                print("App '%s' run error:" % self.name)
                if hasattr(sys, "print_exception"):
                    sys.print_exception(exc)
                else:
                    print(exc)
                # Recover to background so the launcher takes over (don't spin).
                self.active_foreground = False
                self.active_background = True
                await aio.sleep_ms(300)

    def run_foreground(self):
        """App behavior when running in the foreground."""

    def run_background(self):
        """App behavior when running in the background."""

    def switch_to_foreground(self):
        """Set the app as the active foreground app."""
        self.active_foreground = True
        self.active_background = False
        if self.task is None:
            self.start()
        print(f"{self.name} is now the active foreground app.")

    def switch_to_background(self):
        """Set the app as a background app."""
        self.active_background = True
        self.active_foreground = False
        if self.task is None:
            self.start()
        gc.collect()
        print(f"{self.name} is now running in the background.")
