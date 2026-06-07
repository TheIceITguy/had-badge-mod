"""Tiny dependency-free async WebUI for configuration + Python-file updates.

Serves a small config page (settings, device name, WiFi, mesh/region, node list)
and accepts .py app/OS file uploads followed by a reboot. No full .bin OTA.

The routing logic is a pure function (route()) so it is host-testable; the async
server just does socket I/O around it. Runs cooperatively with LVGL and the LoRa
stack: handlers await between reads/writes and never block.
"""
try:
    import asyncio as aio
except ImportError:  # pragma: no cover
    import uasyncio as aio

import json

from core.services import Service
from core.events import EV_WIFI_STATE
from core.settings import Setting, TYPE_BOOL
from core import about

WEB_ROOT = "web"

_CTYPES = {".html": "text/html", ".css": "text/css", ".js": "application/javascript"}


def register_web_settings(settings):
    settings.register(Setting("web_enabled", TYPE_BOOL, True, "WebUI enabled", "WiFi",
                              help="Serves the config page on port 80 when WiFi is up."))


# --- pure helpers (host-testable) --------------------------------------
def safe_upload_path(path):
    """Validate an upload target: a single .py file, at root or under apps/."""
    if not path:
        return None
    path = path.strip().lstrip("/")
    if ".." in path or "\\" in path or not path.endswith(".py"):
        return None
    parts = path.split("/")
    if len(parts) == 1 and parts[0]:
        return parts[0]
    if len(parts) == 2 and parts[0] == "apps" and parts[1]:
        return "apps/" + parts[1]
    return None


def query_get(query, key):
    for pair in (query or "").split("&"):
        if "=" in pair:
            k, v = pair.split("=", 1)
            if k == key:
                return v
    return None


def _parse_json(body):
    if not body:
        return {}
    if isinstance(body, (bytes, bytearray)):
        body = body.decode("utf-8")
    return json.loads(body)


def _json_resp(obj, status="200 OK"):
    return status, "application/json", json.dumps(obj).encode("utf-8")


def _file_resp(path):
    ext = path[path.rfind("."):] if "." in path else ""
    try:
        with open(path, "rb") as f:
            return "200 OK", _CTYPES.get(ext, "text/plain"), f.read()
    except OSError:
        return "404 Not Found", "text/plain", b"not found"


def _status(badge):
    out = {"name": badge.device_name(), "project": about.PROJECT_NAME,
           "version": about.VERSION, "repo": about.REPO_URL, "node": "!%08x" % badge.node_id}
    for key in ("net", "gps", "battery", "wifi"):
        svc = badge.services.get(key)
        if svc is not None:
            try:
                out[key] = svc.status()
            except Exception:  # noqa: BLE001
                pass
    return out


def _nodes(badge):
    mesh = badge.services.get("mesh")
    if mesh is None:
        return []
    out = []
    for n in mesh.nodes():
        out.append({"num": "!%08x" % n.num, "name": n.name(),
                    "lat": n.lat, "lon": n.lon, "snr": n.snr, "last_heard": n.last_heard})
    return out


def route(badge, method, path, query, body):
    """Return (status, content_type, body_bytes). Pure except for file I/O."""
    if method == "GET":
        if path in ("/", "/index.html"):
            return _file_resp(WEB_ROOT + "/index.html")
        if path in ("/style.css", "/app.js"):
            return _file_resp(WEB_ROOT + path)
        if path == "/api/settings":
            return _json_resp(badge.settings.as_dict())
        if path == "/api/status":
            return _json_resp(_status(badge))
        if path == "/api/nodes":
            return _json_resp(_nodes(badge))
        return "404 Not Found", "text/plain", b"not found"

    if method == "POST":
        if path == "/api/settings":
            try:
                data = _parse_json(body)
            except Exception:  # noqa: BLE001
                return _json_resp({"ok": False, "errors": ["bad json"]}, "400 Bad Request")
            errors = badge.settings.update_from_dict(data)
            return _json_resp({"ok": not errors, "errors": errors})
        if path == "/api/wifi":
            try:
                data = _parse_json(body)
            except Exception:  # noqa: BLE001
                data = {}
            badge.settings.update_from_dict(data)
            wifi = badge.services.get("wifi")
            if wifi is not None:
                try:
                    wifi.disable()
                    if badge.settings.get("wifi_enabled", False):
                        wifi.apply()
                except Exception as exc:  # noqa: BLE001
                    return _json_resp({"ok": False, "errors": [str(exc)]}, "500 Error")
            return _json_resp({"ok": True})
        if path == "/api/upload":
            target = safe_upload_path(query_get(query, "path"))
            if not target:
                return _json_resp({"ok": False, "error": "bad path"}, "400 Bad Request")
            try:
                with open(target, "wb") as f:
                    f.write(body)
                return _json_resp({"ok": True, "path": target})
            except Exception as exc:  # noqa: BLE001
                return _json_resp({"ok": False, "error": str(exc)}, "500 Error")
        if path == "/api/reboot":
            _schedule_reboot()
            return _json_resp({"ok": True})
    return "404 Not Found", "text/plain", b"not found"


def _schedule_reboot():
    try:
        import machine

        async def _do():
            await aio.sleep(1)
            machine.reset()
        aio.create_task(_do())
    except Exception:  # noqa: BLE001 - e.g. no running loop on host
        pass


class WebService(Service):
    name = "web"

    def __init__(self, badge):
        super().__init__(badge)
        register_web_settings(badge.settings)
        self._server_task = None

    def start(self):
        super().start()
        wifi = self.badge.services.get("wifi")
        available = wifi is not None and wifi.status().get("available")
        wifi_on = bool(self.settings.get("wifi_enabled", False))
        if available and wifi_on and bool(self.settings.get("web_enabled", True)):
            self._server_task = aio.create_task(self._serve())

    async def _serve(self):
        try:
            await aio.start_server(self._handle, "0.0.0.0", 80)
            print("WebUI on :80")
        except Exception as exc:  # noqa: BLE001
            print("web: start_server failed:", exc)
            return
        while True:
            await aio.sleep(3600)

    async def _handle(self, reader, writer):
        try:
            request_line = await reader.readline()
            if not request_line:
                return
            try:
                method, target, _ = request_line.decode().split(" ", 2)
            except ValueError:
                return
            path, _, query = target.partition("?")
            clen = 0
            while True:
                header = await reader.readline()
                if header in (b"\r\n", b"\n", b""):
                    break
                low = header.decode().lower()
                if low.startswith("content-length:"):
                    try:
                        clen = int(low.split(":", 1)[1].strip())
                    except ValueError:
                        clen = 0
            body = b""
            while len(body) < clen:
                chunk = await reader.read(clen - len(body))
                if not chunk:
                    break
                body += chunk
            status, ctype, out = route(self.badge, method, path, query, body)
            if isinstance(out, str):
                out = out.encode("utf-8")
            head = ("HTTP/1.0 %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
                    "Connection: close\r\n\r\n" % (status, ctype, len(out)))
            writer.write(head.encode("utf-8"))
            writer.write(out)
            await writer.drain()
        except Exception as exc:  # noqa: BLE001
            print("web handler:", exc)
        finally:
            try:
                writer.close()
                await writer.wait_closed()
            except Exception:  # noqa: BLE001
                pass
