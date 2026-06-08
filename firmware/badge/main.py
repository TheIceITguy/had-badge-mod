import micropython
import time
import asyncio as aio  # type: ignore

try:
    from hardware.badge import Badge
    from net.net import badgenet, capture_all_packets
    from net.backend import MessageRouter
    from net.backend_badgenet import BadgeNetBackend
    from net.mesh.backend_meshtastic import MeshtasticBackend
    from services.mesh_service import MeshService
    from apps import app_manager, launcher, msg_app, settings_app
except Exception as ex:
    # If anything goes wrong at import time, wait a second and print it
    # Sometimes these are hard to see, so the delay and extra print may help
    import sys
    import time

    time.sleep(1)
    sys.print_exception(ex)
    raise


async def main():
    print("Initializing main...")
    badge = Badge()
    badgenet.init(badge)

    # Backend-agnostic messaging layer. The router owns whichever LoRa stack is
    # active (one at a time). BadgeNet is registered here; the Meshtastic backend
    # registers itself in a later milestone and may become the default.
    net_router = MessageRouter(badge)
    net_router.register_backend(BadgeNetBackend(badge))
    net_router.register_backend(MeshtasticBackend(badge))
    badge.services.register(net_router)
    badge.net_router = net_router
    # Meshtastic is the default stack (true interop); switchable in settings.
    net_router.set_backend(badge.settings.get("net_backend", "meshtastic"))

    # Mesh node database + periodic NodeInfo/position beacon.
    mesh_service = MeshService(badge)
    badge.services.register(mesh_service)
    mesh_service.start()

    # Persistent left status sidebar (battery/wifi/mesh/gps) on layer_top.
    # Start before any app builds a screen so the theme styles + sidebar exist.
    from services.status_service import StatusService
    status_service = StatusService(badge)
    badge.services.register(status_service)
    status_service.start()

    # Discover any apps dropped into /apps (GPS, Compass, Track, NodeMap, plus
    # user-added apps) via the dynamic scanner, then build the home app list.
    app_scan = app_manager.AppManager("Apps", badge)
    home_apps = [msg_app.App("Messages", badge)]
    home_apps += app_scan.apps
    home_apps.append(settings_app.App("Config", badge))

    home = launcher.Launcher("Home", badge, home_apps)
    for app in home_apps:
        app.start()
    home.start()
    home.switch_to_foreground()

    # To capture all network packets for debugging, set to True
    capture_all_packets(False)
    print("Badge is up and running!")
    print("If you want the Python REPL, try one Ctrl-C or one Ctrl-D")

    while True:
        await aio.sleep(60)
        print("Main 60s heartbeat --^v--^v--")
        micropython.mem_info()


if __name__ == "__main__":
    aio.run(main())
