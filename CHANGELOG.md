# Changelog

All notable changes to the APIOTA ESP32 library are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/), and this
project uses [Semantic Versioning](https://semver.org/).

## [1.4.2] - 2026-06-23

### Changed
- `BasicAPIOTA`: simplified back to a true minimal example — WiFi + OTA + commands +
  a one-line approval gate (`if (!APIOTA.isApproved()) { delay(500); return; }`).
  The Device Config / blink_ms demo lives in the README and the Dashboard's code example.

No library code changes — example only.

## [1.4.1] - 2026-06-11

### Changed
- `LILYGO_TSIM_A7670G` (standalone) now supports the **Approval/Lock gate** (`g_devReady` +
  loop hold with fast-blink LED) and **device name sync** (new `DEVICE_NAME` define, sent
  with `check-update`) — same behaviour as the library-based examples in v1.4.0.

## [1.4.0] - 2026-06-11

### Added
- **Approval / Lock gate** — `APIOTA.isApproved()` (alias `isReady()`): `false` while the device
  is **pending ✓ Approve** or **Locked** from the Dashboard, `true` once approved/unlocked
  (detected automatically via the command poll). Gate your `loop()` with it to hold the
  application until the owner approves the device:
  ```cpp
  void loop() {
    APIOTA.tick();
    if (!APIOTA.isApproved()) { delay(100); return; }   // hold while pending / locked
    // your application code
  }
  ```
  All WiFi examples now demonstrate this: `BasicAPIOTA` / `BasicMultiTask` / `BasicWiFiPortal`
  (fast-blink LED while waiting) and `LILYGO_TDisplayS3` (full-screen **WAITING APPROVAL**
  notice on the TFT, restored automatically after approve/unlock).
  `LILYGO_TSIM_A7670G` is standalone (does not use the library) — gate coming in a later patch.
- **Device name sync** — `check-update` now reports the sketch's `DEVICE_NAME` (`&name=` query).
  Change `DEVICE_NAME` in your sketch and reflash → the name shown in the Dashboard updates
  automatically at boot and on the next OTA check. Older library versions simply don't send
  the name (no breaking change).

## [1.3.1] - 2026-06-11

### Changed
- `BasicAPIOTA` example now demonstrates **Device Config**: `onConfig()` + `configGetInt("blink_ms", 0)` —
  change the LED blink rate live from the Dashboard (⚙️ Device Config → Save) without reflashing.
  Loop is non-blocking (`delay(20)` instead of `delay(1000)`).

No library code changes — examples only.

## [1.3.0] - 2026-06-11

### Added
- **Device Config support** — read key-value settings set in the Dashboard (⚙️ Device Config):
  - `APIOTA.onConfig(cb)` — callback fires once at boot and instantly every time you press
    **Save Config** in the Dashboard (the server pushes a `config_update` command through the
    idle long-poll, so no polling loop is needed).
  - `APIOTA.configGet(key, def)` / `configGetInt(key, def)` / `configGetFloat(key, def)` —
    read values from the cached config with defaults.
  - `APIOTA.fetchConfig()` — manual refresh; `APIOTA.getConfigJson()` — raw cached JSON.
- `config_update` is now a built-in command (handled automatically, like `reboot` / `check_update`).

No breaking changes — existing sketches work unchanged.

## [1.2.3] - 2026-06-11

### Changed
- Renamed examples for a logical listing order in the Arduino IDE and on GitHub:
  `BasicOTA` → `BasicAPIOTA`, `MultiTask` → `BasicMultiTask`, `WiFiPortal` → `BasicWiFiPortal`
  (final order: `BasicAPIOTA`, `BasicMultiTask`, `BasicWiFiPortal`, `LILYGO_TDisplayS3`, `LILYGO_TSIM_A7670G`).

No functional or API changes.

## [1.2.2] - 2026-06-10

### Added
- `MultiTask` example — OTA + command polling run in a FreeRTOS task; `loop()` stays free and uses no `delay()`.
- `WiFiPortal` example — multitasking OTA plus a WiFiManager captive portal for Wi-Fi setup (no hardcoded credentials). Requires the WiFiManager library.

### Changed
- `LILYGO_TDisplayS3` example: default `CURRENT_VERSION` reset to `1.0.0`.

## [1.2.1] - 2026-06-10

### Changed
- Translated all in-code comments to English across the core library (`APIOTAClient.h`, `APIOTADisplay.h`) and every example, plus the on-device status strings — for international users.
- `BasicOTA` example: default `FIRMWARE_VERSION` reset to `1.0.0` as a clean starting point.

No functional or API changes.

## [1.2.0] - 2026-06-09

First public release.

### Features
- Secure OTA firmware updates: RSA-2048 signature + SHA-256 checksum verified over TLS (ISRG Root X1 pinned), abort-safe on failure.
- Zero-touch device provisioning with API key; chip-ID binding (anti-clone).
- Remote command channel via long-poll queue (`led_on`, `reboot`, custom commands).
- Flap-free OTA rollout driven by a rollout counter (safe deploy / rollback).
- Credential storage in NVS across reboots.
- Telemetry: push arbitrary JSON to the Dashboard console.
- Optional TFT display module (`APIOTADisplay`) for LILYGO T-Display-S3.
- Header-only; works with any ESP32 board, no external deps for the core.

### Examples
- `BasicOTA` — minimal Wi-Fi + OTA + LED command.
- `LILYGO_TDisplayS3` — full on-screen UI + Wi-Fi portal.
- `LILYGO_TSIM_A7670G` — OTA over 4G cellular.

[1.2.0]: https://github.com/devjune-apiota/apiota-esp32/releases/tag/v1.2.0
