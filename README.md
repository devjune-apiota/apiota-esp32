# APIOTA — ESP32 OTA Library

[![License: MIT](https://img.shields.io/badge/License-MIT-22E07A.svg)](LICENSE)
[![Platform: ESP32](https://img.shields.io/badge/platform-ESP32-blue.svg)](https://www.espressif.com/)
[![Arduino Library](https://img.shields.io/badge/Arduino-library-00979D.svg)](library.properties)

Header-only Arduino library that gives any **ESP32** board secure **Over-The-Air (OTA)
firmware updates**, zero-touch device provisioning, and a remote command channel — in
about three lines of code.

```cpp
#include <APIOTA.h>
APIOTAClient APIOTA;
void setup(){ APIOTA.connectWiFi("ssid","pass"); APIOTA.begin("ak_xxx","1.0.0","My Device"); }
void loop(){ APIOTA.tick(); }
```

The library talks to an [APIOTA](https://apiota.net) backend (hosted, or your own —
the server URL is overridable). OTA images are verified with **RSA-2048 signatures**
and a **SHA-256 checksum** over **TLS** before flashing, so a bad or tampered image is
rejected and the device stays on its current firmware.

> APIOTA is a **generic ESP32/Arduino OTA platform**. Trackers, EV/GPS units, sensors,
> displays — they're all just example use-cases. Nothing here is tied to a specific
> vertical.

---

## Features

- **Secure OTA** — RSA-2048 signature + SHA-256 verify + TLS (ISRG Root X1 pinned), abort-safe on failure
- **Zero-touch provisioning** — device registers itself with an API key; bind to a chip ID (anti-clone)
- **Remote commands** — long-poll command queue from the Dashboard (`led_on`, `reboot`, custom…)
- **Flap-free rollout** — updates driven by a rollout counter, so deploy / rollback won't bounce versions
- **Credential storage** — device id/secret kept in NVS across reboots
- **Telemetry** — push arbitrary JSON back to the Dashboard console
- **Optional TFT display module** (`APIOTADisplay`) for LILYGO T-Display-S3
- **Header-only**, works with any ESP32 board, no external deps for the core

---

## Installation

**Option A — Arduino IDE, Add .ZIP Library**
1. Download this repo as a ZIP (green **Code** button → *Download ZIP*).
2. Arduino IDE → `Sketch` → `Include Library` → `Add .ZIP Library…` → pick the ZIP.

**Option B — git clone into your Arduino libraries folder**
```bash
git clone https://github.com/<your-account>/apiota-esp32.git \
  ~/Documents/Arduino/libraries/apiota-esp32
```
Restart the Arduino IDE.

### Dependencies

The **core library depends only on the ESP32 Arduino core** (`WiFi`, `Update`,
`Preferences`, `mbedtls/*`). Some examples need extra libraries — install them from the
Arduino **Library Manager**:

| Example | Extra library | Why |
|---|---|---|
| `BasicOTA` | _(none)_ | uses the built-in `connectWiFi()` helper |
| `BasicMultiTask` | _(none)_ | OTA in a FreeRTOS task, no `delay()` |
| `WiFiPortal` | [WiFiManager](https://github.com/tzapu/WiFiManager) | multitasking OTA + Wi-Fi captive portal |
| `LILYGO_TDisplayS3` | [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI), [WiFiManager](https://github.com/tzapu/WiFiManager) | on-screen UI + Wi-Fi captive portal |
| `LILYGO_TSIM_A7670G` | [TinyGSM](https://github.com/vshymanskyy/TinyGSM) | OTA over a 4G/cellular modem |

> **T-Display-S3:** configure `TFT_eSPI`'s `User_Setup` for the ST7789 8-bit parallel
> panel. Pin to a known-good TFT_eSPI version for this board (newer versions can break the display).

---

## Quick start

1. Sign in at [apiota.net](https://apiota.net) → **Provisioning** → create an **API Key** (`ak_...`).
2. Open an example matching your board: `File` → `Examples` → `APIOTA`.
3. In the **USER CONFIG** block, set:
   - `API_KEY` — your key from the Dashboard
   - `FIRMWARE_VERSION` / `CURRENT_VERSION` — **bump this every time you build**
4. Upload. The device registers itself and appears in the Dashboard → **Approve** it.
5. To ship an update: upload a new `.bin` (higher version) under **Firmware**. Devices
   pull it automatically on their next check.

> Export the plain app image (*Sketch → Export Compiled Binary* → `*.ino.bin`).
> Don't upload `*.merged.bin` / bootloader images.

### Self-hosting / custom server

Point the library at your own backend before including it:

```cpp
#define APIOTA_DEFAULT_SERVER "https://ota.example.com"
#define APIOTA_DEFAULT_PUBKEY "-----BEGIN PUBLIC KEY-----\n...your key...\n-----END PUBLIC KEY-----\n"
#include <APIOTA.h>
```

---

## Examples

| Example | Board | Display | Notes |
|---|---|---|---|
| **BasicOTA** | any ESP32 | — | minimal: Wi-Fi + OTA + `led_on`/`led_off` command |
| **BasicMultiTask** | any ESP32 | — | OTA in a FreeRTOS task — no `delay()`, `loop()` stays free |
| **WiFiPortal** | any ESP32 | — | multitasking OTA + WiFiManager captive portal (no hardcoded Wi-Fi) |
| **LILYGO_TDisplayS3** | LILYGO T-Display-S3 | yes | full on-screen UI, Wi-Fi portal, status screens |
| **LILYGO_TSIM_A7670G** | LILYGO T-SIM A7670G | — | OTA over 4G cellular |

---

## API reference

```cpp
APIOTA.setCheckInterval(seconds);     // how often to check for updates
APIOTA.connectWiFi(ssid, pass);       // optional Wi-Fi helper
APIOTA.begin(apiKey, version, name);  // provision + start OTA + command poll
APIOTA.tick();                        // call from loop()

APIOTA.onCommand([](const String& cmd, const String& payload, uint32_t id){ ... });
APIOTA.onProgress([](int percent){ ... });
APIOTA.onExpired([](const String& reason){ ... });
APIOTA.sendTelemetry("{\"temp\":25}");
```

`reboot` and `check_update` commands are handled by the library automatically.

### Device states

- **Locked** (from Dashboard) → device pauses; resumes on **Unlock** without a reset.
- **Expired / over quota** → device stops with a reason; the callbacks above let you react.

---

## Project layout

```
apiota-esp32/
├── src/
│   ├── APIOTA.h          entry point (include this one)
│   ├── APIOTAClient.h    core: provision / OTA / verify / command / lock
│   └── APIOTADisplay.h   TFT display module (T-Display-S3)
├── examples/
├── library.properties
├── keywords.txt
└── LICENSE
```

---

## ภาษาไทย (สรุปย่อ)

ไลบรารี ESP32 สำหรับอัปเดตเฟิร์มแวร์แบบ OTA ผ่าน [apiota.net](https://apiota.net) อย่างปลอดภัย
(ตรวจลายเซ็น RSA-2048 + SHA-256 ผ่าน TLS), ลงทะเบียนอุปกรณ์อัตโนมัติ, รับคำสั่งจาก Dashboard,
เก็บ credential ใน NVS และมีโมดูลจอ TFT สำหรับ LILYGO T-Display-S3

**เริ่มใช้:** สมัครที่ apiota.net → สร้าง API Key (`ak_...`) → เปิดตัวอย่างใน `File → Examples → APIOTA`
→ ใส่ `API_KEY` + เลขเวอร์ชัน (เพิ่มทุกครั้งที่ build) → อัปโหลด → กด Approve ใน Dashboard
อัปเดต OTA โดยอัปไฟล์ `.bin` เวอร์ชันใหม่ (`*.ino.bin` เพียวๆ) ที่หน้า Firmware

**ติดตั้งไลบรารีเสริม** (เฉพาะบางตัวอย่าง) จาก Library Manager: TFT_eSPI + WiFiManager
(T-Display-S3), TinyGSM (T-SIM 4G) — core ไม่ต้องลงอะไรเพิ่มนอกจาก ESP32 Arduino core

---

## Contributing

Issues and pull requests are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT](LICENSE) © 2026 APIOTA · Platform & docs: [apiota.net](https://apiota.net)
