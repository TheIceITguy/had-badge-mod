"""Host tests for the mesh node database (no asyncio beacon loop)."""
from core.events import EventBus
from core.services import ServiceRegistry
from core.settings import SettingsRegistry
from net.backend import Message
from services.mesh_service import MeshService


class FakeConfig:
    def __init__(self):
        self.d = {}

    def get(self, k, default=None):
        return self.d.get(k, default)

    def set(self, k, v):
        self.d[k] = v

    def flush(self):
        pass


class FakeBadge:
    def __init__(self):
        self.events = EventBus()
        self.settings = SettingsRegistry(FakeConfig())
        self.services = ServiceRegistry(self)
        self.net_router = None


def test_node_db_from_nodeinfo_and_position():
    m = MeshService(FakeBadge())
    m._on_message(Message.nodeinfo_msg(0xABCD, "Lecco Node", "LEC"))
    m._on_message(Message.position_msg(45.8566, 9.3976, alt=214, from_id=0xABCD))
    rec = m.node(0xABCD)
    assert rec.long_name == "Lecco Node" and rec.short_name == "LEC"
    assert abs(rec.lat - 45.8566) < 1e-6 and rec.alt == 214
    assert rec.has_position()
    assert m.count() == 1


def test_ignores_none_and_missing_from():
    m = MeshService(FakeBadge())
    m._on_message(None)
    m._on_message(Message.text_msg("hi", from_id=None))
    assert m.count() == 0


def test_name_fallback_to_hex_id():
    m = MeshService(FakeBadge())
    m._on_message(Message.text_msg("hi", from_id=0x12345678))
    assert m.node(0x12345678).name() == "!12345678"


def test_publishes_node_update_event():
    badge = FakeBadge()
    seen = []
    from core.events import EV_MESH_NODE_UPDATE
    badge.events.subscribe(EV_MESH_NODE_UPDATE, lambda rec: seen.append(rec.num))
    m = MeshService(badge)
    m._on_message(Message.nodeinfo_msg(0x42, "X", "X"))
    assert seen == [0x42]
