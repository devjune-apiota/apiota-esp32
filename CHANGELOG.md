# Changelog

All notable changes to the APIOTA ESP32 library are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/), and this
project uses [Semantic Versioning](https://semver.org/).

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
