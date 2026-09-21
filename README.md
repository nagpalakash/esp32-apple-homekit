# esp32-apple-homekit

Native Apple HomeKit accessories on ESP32 WROOM-32, built with
[ESPHap](https://github.com/Yurik72/ESPHap). Each folder is a standalone Arduino sketch.

## Projects

| Folder | What it does |
| --- | --- |
| `ESP32_HomeKit_Switches` | Five accessories: main light, night lamp, dim light (pulsed relays) and two IR remote keys for the side lights. Includes a web control page and OTA update. |
| `ESP32_HomeKit_DecoLight` | Minimal single switch **Deco Light** (HomeSpan). Hostname `Deco-Light.local`. |
| `ESP32_HomeKit_PhotoLight` | Minimal single switch **Photo Light** (HomeSpan). Hostname `Photo-Light.local`. |

## Credentials

WiFi credentials are never committed. Every sketch folder has a `secrets.example.h`;
copy it to `secrets.h` in the same folder and fill in your values:

```
copy ESP32_HomeKit_DecoLight\secrets.example.h ESP32_HomeKit_DecoLight\secrets.h
```

`.gitignore` excludes every `secrets.h`. The sketches fall back to empty defaults when
the file is absent, and fail the build with a clear message if WiFiManager is disabled
and no `secrets.h` exists. With WiFiManager enabled (the default) the device serves a
captive portal instead, so the values in `secrets.h` are only a fallback.

## HomeKit pairing codes (defaults)

| Project | Setup / pairing code |
| --- | --- |
| `ESP32_HomeKit_DecoLight` (HomeSpan) | **`466-37-726`** |
| `ESP32_HomeKit_PhotoLight` (HomeSpan) | **`466-37-726`** |
| `ESP32_HomeKit_Switches` (ESPHap) | **`111-11-111`** |

In the Apple Home app: **Add Accessory → More options →** select the device → enter the code above.

For Deco Light you can also open Serial Monitor @ **115200**, type **`H`**, and press Enter to print the setup code.

## Dependencies

| Project | Stack |
| --- | --- |
| `ESP32_HomeKit_DecoLight` | [HomeSpan](https://github.com/HomeSpan/HomeSpan) (works with ESP32 Arduino core 3.x) |
| `ESP32_HomeKit_PhotoLight` | [HomeSpan](https://github.com/HomeSpan/HomeSpan) (works with ESP32 Arduino core 3.x) |
| `ESP32_HomeKit_Switches` | [ESPHap](https://github.com/Yurik72/ESPHap) + patched wolfSSL + WiFiManager + IRremote 4.x (needs older ESP32 core ~1.0.6 / 2.0.x — not 3.3.x) |

### HomeSpan flash settings (Deco Light / Photo Light)
- Board: **ESP32 Dev Module**
- Partition Scheme: **Minimal SPIFFS** (default is too small)
- Port: your CP210x COM port
- Pairing code: **`466-37-726`** (confirm with Serial Monitor `H` if needed)

## Scripts (Windows)

From the repo root:

```bat
scripts\esp32.cmd ports
scripts\esp32.cmd flash photo
scripts\esp32.cmd flash deco
scripts\esp32.cmd status
scripts\esp32.cmd monitor
scripts\esp32.cmd ping
scripts\esp32.cmd unpair
```

Or PowerShell:

```powershell
.\scripts\esp32.ps1 flash photo
.\scripts\esp32.ps1 flash deco
.\scripts\esp32.ps1 status -Port COM5
```

Shortcuts: `scripts\flash-photo.cmd`, `scripts\flash-deco.cmd`, `scripts\status.cmd`.
Requires [arduino-cli](https://arduino.github.io/arduino-cli/) and `pyserial` (`pip install pyserial`) for status/unpair.

### Dual-band WiFi (single SSID)
ESP32 only does **2.4 GHz**. If your router uses one SSID for 2.4 + 5 GHz:
1. Best: create a separate 2.4 GHz-only SSID for IoT devices, or
2. In `secrets.h` set `WIFI_BSSID` to the **2.4 GHz** AP MAC from the router admin page.
The Deco Light sketch already forces B/G/N + 20 MHz and retries on disconnect.