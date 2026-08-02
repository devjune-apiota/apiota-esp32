# Changelog

All notable changes to the APIOTA ESP32 library are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/), and this
project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- **T-SIM A7670G example: remote commands (⚡Cmd) over cellular.** The 4G
  example now fetches queued Dashboard commands — `reboot` / `check_update`
  are built in, and a `handleCommand()` hook in the sketch handles your own
  (`led_on` / `led_off` ship as samples). Results are acknowledged back to the
  Dashboard. Verified end-to-end on real hardware over LTE.
- **T-SIM A7670G example: external L76K GPS support.** Board variants carrying
  the separate L76K GNSS chip are detected automatically — position cards
  (lat/lon/alt/speed/sats + a map card) attach to telemetry once there is a
  fix, and a `GPS` struct is readable from `loop()` for your own logic
  (geofencing etc.). Modem-GNSS variants keep working as before.

### Changed
- **T-SIM A7670G example restructured around one editable page.** The sketch
  now holds only what users touch — USER CONFIG, telemetry cards, GPS cards,
  command handlers and `loop()` — while the modem/LTE/OTA engine lives in
  `apiota_4g.h` (same folder, never needs editing). Telemetry rounds are ~3x
  faster (only the response verdict is read over the modem), quiet mode
  (`DEBUG_MODE 0`, now the default) reports every send and every received
  command in the Serial Monitor, and a periodic "waiting for ✓ Approve"
  reminder shows while the device is pending. Compile-verified in every
  toggle combination.
- All examples tidied for readability: USER CONFIG moved to the top of each
  sketch, unused includes removed, comments trimmed. No behavior changes —
  every example compile-verified.

## [1.4.5] - 2026-07-24

### Added
- `LILYGO_TDisplayS3_Mini` example — the same on-screen UI as the full
  T-Display-S3 example (TFT status + themes + WiFi portal + dashboard-controlled
  LED/brightness/theme) in ~30 lines via the `APIOTADisplay` module. Customers
  edit one API-key line; the 900-line example remains as the full-control reference.

### Fixed
- **T-Display-S3 black screen out of the box.** The example header listed SPI pin
  defines that are wrong for this board (its ST7789 is wired 8-bit parallel) —
  replaced with the actual 2-line fix: enable TFT_eSPI's bundled
  `Setup206_LilyGo_T_Display_S3.h` in `User_Setup_Select.h`. README now has a
  step-by-step "black screen?" box (verified with TFT_eSPI 2.5.43). Both
  T-Display-S3 examples now also carry an `#error` compile guard: building with
  an unconfigured TFT_eSPI fails immediately with the 2-line fix in the error
  message, instead of compiling fine and showing a black screen.
- **"WAITING APPROVAL" / blocked screen was overpainted until unreadable.** While
  a blocking notice (pending approval / locked) was on screen, the display task
  kept repainting live stats over it every event and every 10 s. The example now
  holds background repaints while a blocking screen is shown and redraws the full
  UI automatically the moment the device is approved or unlocked — verified on
  real hardware.
- **T-Display-S3 LCD power-enable pin now driven explicitly.** The example and
  `APIOTADisplay::begin()` now drive GPIO 15 (the board's PWR_EN / LCD power
  rail) HIGH at startup, as LILYGO's own examples do. Override with
  `#define APIOTADISP_PWR_EN -1` for other boards.
- A device stopped by the server (plan limit, working period or lifetime expired)
  parked its poll task in a 60-second sleep forever — after the owner renewed or
  upgraded the plan, the device stayed dead until a power cycle. The poll task now
  sends one real poll roughly every 15 minutes as a probe; when the server accepts
  again the device clears the block and resumes automatically ("plan restored").
  Still-expired probes are rejected cheaply at the auth layer (402) and simply
  re-arm the block, so the added server load is negligible.

## [1.4.4] - 2026-07-20

### Fixed
- A device whose provisioning failed (WiFi slower than the connect timeout, or the
  server briefly unreachable at boot) was permanently dead: `begin()` returned `false`
  and `tick()` silently did nothing forever. `tick()` now retries auto-provisioning on
  a 30s→15min backoff (fast-reset after a WiFi reconnect); on success the command poll
  task and Device Config fetch start automatically — no reboot needed.
- Same self-heal applies when the background poll task must re-provision (device
  deleted from the dashboard, credentials revoked) and that one attempt fails —
  previously the poll task never tried again until a power cycle.

- When the owner removes a device from the dashboard, the device now backs off and waits
  for restore instead of wiping NVS and re-registering (which used to silently reclaim the
  freed plan slot). The server soft-deletes and returns a `device_removed` signal; the
  device recovers automatically if the owner restores it, with no reflash.

### Notes
- No API changes — existing sketches work unchanged. Sketches that ignore `begin()`'s
  return value and keep calling `tick()` in `loop()` (all Basic examples) get the retry
  for free. While unprovisioned, each retry attempt runs synchronously inside `tick()`
  and can block `loop()` for a few seconds (TLS + HTTP timeout); this only happens on a
  device that would otherwise be dead. `onError`/`onProgress` callbacks may now fire on
  each retry attempt rather than once at boot.

## [1.4.3] - 2026-07-02

### Fixed
- Stripped stray NUL bytes that had crept in at the end of
  `examples/LILYGO_TSIM_A7670G/LILYGO_TSIM_A7670G.ino` (harmless — the compiler ignored
  them with a "null character(s) ignored" warning — but the file is clean now).

### Changed
- `keywords.txt` refreshed for the v1.3/v1.4 API so the Arduino IDE highlights it:
  `onConfig` / `onExpired` / `configGet` / `configGetInt` / `configGetFloat` / `fetchConfig` /
  `getConfigJson` / `isApproved` / `isReady` / `sendTelemetry` / `connectWiFi` / `setCACert` /
  `setInsecure` / `bindOTA` / `handleCommand`, the `APIOTADisplay` / `APIOTAExpiry` types and
  the `APEX_*` constants.

No library code changes — existing sketches work unchanged.

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
