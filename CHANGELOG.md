# Changelog

All notable changes to the APIOTA ESP32 library are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/), and this
project uses [Semantic Versioning](https://semver.org/).

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
