"""Map ``uasyncio`` onto the host's ``asyncio`` for tests, adding MicroPython extras."""
from asyncio import *  # noqa: F401,F403
import asyncio as _asyncio


async def sleep_ms(ms):
    await _asyncio.sleep(ms / 1000)


def create_task(coro):
    return _asyncio.ensure_future(coro)


class ThreadSafeFlag:
    def __init__(self):
        self._event = _asyncio.Event()

    def set(self):
        self._event.set()

    def clear(self):
        self._event.clear()

    async def wait(self):
        await self._event.wait()
        self._event.clear()
