"""Host tests for the EventBus."""
from core.events import EventBus


def test_order_and_payload():
    bus = EventBus()
    seen = []
    bus.subscribe("e", lambda p: seen.append(("a", p)))
    bus.subscribe("e", lambda p: seen.append(("b", p)))
    bus.publish("e", 42)
    assert seen == [("a", 42), ("b", 42)]


def test_exception_isolation():
    bus = EventBus()
    seen = []

    def bad(_):
        raise RuntimeError("boom")

    bus.subscribe("e", bad)
    bus.subscribe("e", lambda p: seen.append(p))
    bus.publish("e", "ok")  # must not raise
    assert seen == ["ok"]


def test_unsubscribe_and_count():
    bus = EventBus()
    h = lambda p: None  # noqa: E731
    bus.subscribe("e", h)
    assert bus.subscriber_count("e") == 1
    bus.unsubscribe("e", h)
    assert bus.subscriber_count("e") == 0
    bus.unsubscribe("e", h)  # idempotent / no error
    bus.publish("nobody")    # no subscribers, no error
