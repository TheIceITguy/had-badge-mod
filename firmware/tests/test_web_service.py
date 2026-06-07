"""Host tests for the WebUI router (pure logic; no sockets)."""
import json

from core.settings import SettingsRegistry, Setting, TYPE_INT, TYPE_ENUM
from core.services import ServiceRegistry
from services import web_service as web


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
        self.settings = SettingsRegistry(FakeConfig())
        self.settings.register_many([
            Setting("tx_power", TYPE_INT, 9, "TX", "Radio", minv=0, maxv=22),
            Setting("region", TYPE_ENUM, "EU_868", "Region", "Radio", choices=["EU_868", "US"]),
        ])
        self.services = ServiceRegistry(self)
        self.node_id = 0x12345678

    def device_name(self):
        return "Badge-Test"


def test_safe_upload_path():
    assert web.safe_upload_path("apps/foo.py") == "apps/foo.py"
    assert web.safe_upload_path("/foo.py") == "foo.py"
    assert web.safe_upload_path("apps/../boot.py") is None
    assert web.safe_upload_path("foo.txt") is None
    assert web.safe_upload_path("a/b/c.py") is None
    assert web.safe_upload_path("") is None


def test_query_get():
    assert web.query_get("path=apps%2Ffoo.py&x=1", "path") == "apps%2Ffoo.py"
    assert web.query_get("x=1", "path") is None


def test_get_settings_json():
    status, ctype, body = web.route(FakeBadge(), "GET", "/api/settings", "", b"")
    assert status == "200 OK" and ctype == "application/json"
    data = json.loads(body)
    assert data["tx_power"]["value"] == 9
    assert data["region"]["choices"] == ["EU_868", "US"]


def test_post_settings_updates():
    badge = FakeBadge()
    body = json.dumps({"tx_power": 17, "region": "US"}).encode()
    status, _ctype, out = web.route(badge, "POST", "/api/settings", "", body)
    assert status == "200 OK"
    assert json.loads(out)["ok"] is True
    assert badge.settings.get("tx_power") == 17
    assert badge.settings.get("region") == "US"


def test_post_settings_reports_errors():
    badge = FakeBadge()
    body = json.dumps({"region": "MARS"}).encode()
    _status, _ctype, out = web.route(badge, "POST", "/api/settings", "", body)
    assert json.loads(out)["ok"] is False


def test_status_and_nodes_and_reboot():
    badge = FakeBadge()
    s, _c, body = web.route(badge, "GET", "/api/status", "", b"")
    assert s == "200 OK"
    st = json.loads(body)
    assert st["name"] == "Badge-Test" and st["node"] == "!12345678"

    s, _c, body = web.route(badge, "GET", "/api/nodes", "", b"")
    assert s == "200 OK" and json.loads(body) == []

    s, _c, body = web.route(badge, "POST", "/api/reboot", "", b"")
    assert s == "200 OK" and json.loads(body)["ok"] is True


def test_upload_bad_path_rejected():
    s, _c, body = web.route(FakeBadge(), "POST", "/api/upload", "path=evil.txt", b"x")
    assert s == "400 Bad Request" and json.loads(body)["ok"] is False
