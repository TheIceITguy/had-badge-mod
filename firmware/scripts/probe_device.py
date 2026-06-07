"""On-device capability probe for the Communicator Badge MicroPython build.

Run on a badge to confirm what this firmware mod can rely on:

    mpremote run firmware/scripts/probe_device.py

or paste the body into the REPL. Every check is defensive: a failure prints the
reason instead of aborting. The two load-bearing answers are:

  * AES-CTR present  -> Meshtastic payload crypto works natively (else ECB fallback)
  * network present  -> WiFi + WebUI are possible on this build
"""


def _line(label, value):
    print("  {:<22} {}".format(label, value))


def probe():
    print("=== Communicator Badge capability probe ===")

    # --- WiFi / network ---------------------------------------------------
    try:
        import network  # noqa: F401
        _line("network (WiFi):", "PRESENT")
        try:
            wl = network.WLAN(network.STA_IF)
            _line("  WLAN(STA_IF):", "ok")
        except Exception as e:  # noqa: BLE001
            _line("  WLAN(STA_IF):", "error: %s" % e)
    except Exception as e:  # noqa: BLE001
        _line("network (WiFi):", "MISSING (%s)" % e)

    # --- AES-CTR via cryptography (ucryptography) -------------------------
    aes_ctr = "MISSING"
    try:
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

        key = bytes(range(16))
        iv = bytes(16)
        enc = Cipher(algorithms.AES(key), modes.CTR(iv)).encryptor()
        ct = enc.update(b"meshtastic-test!") + enc.finalize()
        dec = Cipher(algorithms.AES(key), modes.CTR(iv)).decryptor()
        pt = dec.update(ct) + dec.finalize()
        aes_ctr = "PRESENT (roundtrip ok)" if pt == b"meshtastic-test!" else "BROKEN"
    except Exception as e:  # noqa: BLE001
        aes_ctr = "MISSING (%s)" % e
    _line("AES-CTR (cryptography):", aes_ctr)

    # --- AES-ECB fallback (cryptography, then ucryptolib) -----------------
    ecb = "MISSING"
    try:
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

        Cipher(algorithms.AES(bytes(16)), modes.ECB())
        ecb = "PRESENT (cryptography)"
    except Exception:  # noqa: BLE001
        try:
            import ucryptolib

            ucryptolib.aes(bytes(16), 1)  # mode 1 = ECB
            ecb = "PRESENT (ucryptolib)"
        except Exception as e:  # noqa: BLE001
            ecb = "MISSING (%s)" % e
    _line("AES-ECB fallback:", ecb)

    # --- machine peripherals ---------------------------------------------
    try:
        import machine

        _line("machine.unique_id:", machine.unique_id())
        node = int.from_bytes(machine.unique_id()[2:6], "big")
        _line("  derived node id:", "0x%08x" % node)
        _line("machine.UART:", "yes" if hasattr(machine, "UART") else "no")
        _line("machine.ADC:", "yes" if hasattr(machine, "ADC") else "no")
        _line("machine.RTC:", "yes" if hasattr(machine, "RTC") else "no")
    except Exception as e:  # noqa: BLE001
        _line("machine:", "error: %s" % e)

    # --- free heap --------------------------------------------------------
    try:
        import gc

        gc.collect()
        _line("gc.mem_free:", "%d bytes" % gc.mem_free())
    except Exception as e:  # noqa: BLE001
        _line("gc.mem_free:", "error: %s" % e)

    try:
        import esp32

        _line("esp32.raw_temperature:", esp32.raw_temperature())
    except Exception:  # noqa: BLE001
        pass

    print("=== probe complete ===")


probe()
