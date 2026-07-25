---
title: Settings
description: Every setting the badge exposes, with its key and default, and how the editors work.
---

Settings is schema driven. Every option is declared once in the firmware with a type, a default, a
label, and a group, and the Settings app renders that schema. The same schema is the basis for the web
interface, so the two stay in sync. Values are stored in NVS as strings and read back with typed
accessors, so they survive reboots.

## Editing

Move with the arrow keys and open a setting to edit it. Booleans toggle in place, enumerations open a
list of their choices, and text fields open an editor you can type into. Enter or F1 saves, and Esc
backs out one level. Secrets, the channel keys and the WiFi passwords, are shown masked in the list.

A number opens its own editor: the current value large at the top, a slider showing where the value
sits in its allowed range, the range itself as "Range x to y", and a field to type into. F2 and F4
step the value, the Up and Down arrows do the same, and Enter or F1 saves. A value outside the range
is clamped to the nearest end rather than rejected.

## Groups

The groups appear in this order on the badge, and every setting in each one is listed with the key it
is stored under.

| Group | Setting | Key | Default | Range or choices |
|-------|---------|-----|---------|------------------|
| Network | Network stack | `net_backend` | `meshtastic` | `meshtastic` |
| Radio | LoRa region | `mesh_region` | `EU_868` | US, EU_868, EU_433, ANZ, AU_915, IN, JP, KR, CN, RU, TW, TH, UA_868 |
| Radio | Modem preset | `mesh_preset` | `LongFast` | ShortTurbo, ShortFast, ShortSlow, MediumFast, MediumSlow, LongFast, LongModerate, LongSlow, VeryLongSlow |
| Radio | Channel name | `mesh_chan` | `LongFast` | text |
| Radio | Channel key (b64) | `mesh_psk` | `AQ==` | text, masked |
| Radio | Hop limit | `mesh_hop` | 3 | 0 to 7 |
| Radio | Relay others (router) | `mesh_relay` | off | bool |
| Radio | Share GPS position | `mesh_share_pos` | off | bool |
| Radio | Position interval s | `mesh_pos_int` | 60 | 10 to 3600 |
| Radio | Announce me every (s) | `mesh_info_int` | 300 | 30 to 3600 |
| Radio | TX power dBm | `radio_tx_power` | 9 | -9 to 22 |
| Channels | Channel 2 name | `ch1_name` | empty | text |
| Channels | Channel 2 key (b64) | `ch1_psk` | empty | text, masked |
| Channels | Channel 3 name | `ch2_name` | empty | text |
| Channels | Channel 3 key (b64) | `ch2_psk` | empty | text, masked |
| Channels | Channel 4 name | `ch3_name` | empty | text |
| Channels | Channel 4 key (b64) | `ch3_psk` | empty | text, masked |
| Device | Device name | `device_name` | empty | text |
| Device | Short name | `short_name` | empty | text |
| Power | Dim after (s, 0=off) | `bl_dim_s` | 60 | 0 to 3600 |
| Power | Off after (s, 0=off) | `bl_off_s` | 0 | 0 to 3600 |
| Power | Bright level | `bl_bright` | 700 | 10 to 1023 |
| Power | Dim level | `bl_dim` | 120 | 0 to 1023 |
| Bluetooth | Bluetooth (Meshtastic app) | `ble_enabled` | off | bool |
| Battery | Battery sense | `bat_enabled` | off | bool |
| Battery | Sense GPIO (ADC) | `bat_pin` | 11 | 0 to 48 |
| Battery | Divider ratio x100 | `bat_div_x100` | 200 | 100 to 1000 |
| GPS | GPS enabled | `gps_enabled` | on | bool |
| GPS | GPS RX pin (ESP) | `gps_rx_pin` | 7 | 0 to 48 |
| GPS | GPS TX pin (ESP) | `gps_tx_pin` | 6 | 0 to 48 |
| Compass | IMU compass enabled | `imu_enabled` | on | bool |
| Compass | Declination (0.1 deg, E+) | `mag_decl_ddeg` | 0 | -1800 to 1800, tenths of a degree |
| Compass | Use saved calibration | `mag_cal_use` | on | bool |
| Compass | Compass SDA pin | `imu_sda_pin` | 4 | 0 to 48 |
| Compass | Compass SCL pin | `imu_scl_pin` | 5 | 0 to 48 |
| Compass | I2C address 0x69 (AD0 high) | `imu_addr_hi` | off | bool |
| WiFi | WiFi mode | `wifi_mode` | `off` | off, sta, ap |
| WiFi | WiFi SSID | `wifi_ssid` | empty | text |
| WiFi | WiFi password | `wifi_pass` | empty | text, masked |
| WiFi | Hotspot name | `ap_ssid` | `Communicator` | text |
| WiFi | Hotspot password | `ap_pass` | empty | text, masked |
| WiFi | Web interface | `web_enabled` | on | bool |
| Vibration | Vibration motor | `vibe_enabled` | on | bool |
| Vibration | Motor GPIO | `vibe_pin` | 12 | 0 to 48 |
| Vibration | Buzz length (ms) | `vibe_ms` | 180 | 20 to 2000 |
| Vibration | Buzz on message | `vibe_on_msg` | on | bool |
| LED | Notification LED | `led_enabled` | on | bool |
| LED | LED GPIO | `led_pin` | 1 | 0 to 48 |
| LED | LED is active low | `led_active_lo` | on | bool |
| LED | Blink on unread message | `led_on_msg` | on | bool |
| LED | Idle heartbeat (s, 0=off) | `led_beat_s` | 0 | 0 to 3600 |
| Messages | Saved messages | `msg_keep` | 50 | 0 to 64, 0 disables saving |
| Radar | Map overlay | `radar_map` | off | bool |

The Compass group is documented in full on the [Compass](/had-badge-mod/apps/compass/) page, the GPS
pins on the [GPS](/had-badge-mod/apps/gps/) page.

## Which changes need a restart

Most settings take effect as soon as you leave the editor. The exceptions are the peripherals, which
are brought up once at boot from the values stored then:

- Battery: all three keys
- GPS: `gps_enabled`, `gps_rx_pin`, `gps_tx_pin`
- Compass: `imu_enabled`, `imu_sda_pin`, `imu_scl_pin`, `imu_addr_hi`
- Vibration: all four keys
- LED: all five keys

Two Compass settings are the exception to that exception: `mag_decl_ddeg` and `mag_cal_use` are
re-read about once a second while the badge runs.

Radio settings such as region, preset, channel, and key take effect on the next radio reconfigure or
reboot. If you change the channel and want to talk to a specific device, match its region, channel
name, and key exactly.

## Announce interval

Under Radio, "Announce me every (s)" (`mesh_info_int`) sets how often the badge tells the other nodes
it exists, by broadcasting its node info and telemetry. The default is 300 seconds. You can also send
one straight away with the Announce function key in the Messages and Nodes apps, without changing this
interval.
