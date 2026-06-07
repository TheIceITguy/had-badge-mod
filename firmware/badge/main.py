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
    from apps import app_manager, app_menu, chat, config_manager, usb_debug, nametag, talks, msg_app, settings_app
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

    user_app_manager = app_manager.AppManager("Apps", badge)
    # These apps are on the main screen when the badge boots
    primary_apps = [
        # Messages is the backend-agnostic communicator (Meshtastic or BadgeNet).
        # It supersedes the BadgeNet-only stock Chat app on the home screen.
        msg_app.App("Messages", badge),
        talks.Talks("Talks", badge),
        nametag.App("Nametag", badge),
        user_app_manager,
        # Schema-driven settings page (manage WiFi/GPS/region/backend/power/name).
        settings_app.App("Config", badge),
    ]
    # These apps aren't listed in the menus, so put them here to get started below
    backgrounded_apps = [
        usb_debug.UsbDebug("USB Debug", badge),
    ]
    main_menu = app_menu.AppMenu("Main", badge, primary_apps, True)
    for app in primary_apps:
        if app:
            app.start()
    for app in backgrounded_apps:
        app.start()
    main_menu.start()
    main_menu.switch_to_foreground()

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
