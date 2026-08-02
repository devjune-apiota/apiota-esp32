/*
  ================================================================
   LILYGO_TSIM_A7670G — APIOTA over 4G cellular (no Wi-Fi needed)
   Board: LILYGO T-SIM A7670G (ESP32-WROVER + SIMCom A7670G LTE Cat-1)
  ================================================================

   How to use — 3 steps:
     1. Fill in USER CONFIG below (API key + APN of your SIM)
     2. buildTelemetryData() / handleCommand() — your values & commands
     3. Write your application in loop()

   Everything else (modem power-on, LTE, provisioning, secure OTA,
   GPS + telemetry + ⚡Cmd polling) runs in a background task — see
   apiota_4g.h (same folder, opens as a tab). You never need to edit it.

   Requires: TinyGSM (Library Manager) — v0.12.0+
   GPS: the A7670G modem has no GNSS inside. Board variants with the
   extra L76K GPS chip are detected automatically — positions attach
   to telemetry once there is a fix (connect the GPS antenna; first
   fix outdoors may take 30s-2min).
   Known issue: "[HTTP] connect failed" on every attempt on some A76xx
   firmwares (e.g. A7670G-LLSE) — 1-line TinyGSM fix, see the APIOTA
   README ("T-SIM A7670G known issue").
  ================================================================
*/

// ╔══════════════════════════════════════════════════════════════╗
//   DEBUG TOGGLE — 0 = quiet, important messages only (default)
//                  1 = verbose log + raw AT (troubleshooting)
// ╚══════════════════════════════════════════════════════════════╝
#define DEBUG_MODE  0

// ╔══════════════════════════════════════════════════════════════════╗
//   USER CONFIG — edit only this section
// ╠══════════════════════════════════════════════════════════════════╣
#define API_KEY           "ak_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // from apiota.net Dashboard
#define CURRENT_VERSION   "1.0.0"                // bump on every new build
#define DEVICE_NAME       "TSIM_A7670G"          // shown in Dashboard → Devices

// APN of your SIM — TH: AIS="internet" | DTAC="www.dtac.co.th" | TRUE="internet.true.th"
#define APN               "internet"
#define GPRS_USER         ""
#define GPRS_PASS         ""

// ── intervals — what each one spends from your plan (Dashboard → Plan Usage) ──
#define OTA_CHECK_SEC     300   // OTA check — 1 ⚡API Call each (every 300s)
#define TELEMETRY_SEC     15    // send cards — 1 📡Realtime each (every 15s)
#define COMMAND_POLL_SEC  10    // ⚡Cmd check — FREE while idle (1 📡Realtime only when a command arrives)
#define SEND_GPS          1     // GPS cards + 🗺 map in Console — free (rides on telemetry) | 0 = off
// ╚══════════════════════════════════════════════════════════════════╝

#include "apiota_4g.h"   // ← the engine: modem + 4G + provision + OTA + GPS

// ── TELEMETRY CARDS — every key returned here = one live card in the 🖥 Console ──
String buildTelemetryData() {
  // more cards:  "{\"temp\":25.5}"   "{\"door\":\"open\",\"rssi\":-71}"
  float v = readBatteryVoltage();               // built-in helper (18650 on GPIO 35)
  if (v < 2.5) return "{\"power\":\"usb\"}";    // no battery on the rail
  return "{\"batt_v\":" + String(v, 2) + "}";
}

// ── GPS CARDS — sent while SEND_GPS = 1 and the GPS has a fix ────
//    lat + lon together make the 🗺 map card. Delete any line you
//    don't want. GPS.lat / GPS.lon are also usable in loop().
String buildGpsData() {
  return "\"lat\":"    + String(GPS.lat, 6)
       + ",\"lon\":"   + String(GPS.lon, 6)
       + ",\"alt\":"   + String(GPS.alt, 1)     // altitude, m
       + ",\"speed\":" + String(GPS.speed, 1)   // km/h
       + ",\"sats\":"  + String(GPS.sats);      // satellites in use
}

// ── COMMANDS — ⚡Cmd button in the Dashboard ─────────────────────
//    reboot / check_update are handled automatically; add yours here.
//    Return true = done (Dashboard shows "acked" + your result text).
bool handleCommand(const String& cmd, const String& payload, String& result) {
  if (cmd == "led_on")  { digitalWrite(LED_PIN, HIGH); result = "led is on";  return true; }
  if (cmd == "led_off") { digitalWrite(LED_PIN, LOW);  result = "led is off"; return true; }
  return false;   // unknown command → Dashboard shows "failed"
}

// ── SETUP ────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  apiotaStart();   // starts the 4G + OTA background task (core 0) — loop() stays free
}

// ── LOOP — your application code (never blocked by network/OTA) ──
void loop() {
  if (apiotaHold()) return;   // new device: hold here until ✓ Approve in Dashboard
                              // (LED blinks fast while waiting — resumes by itself)

  // ───────────── put your own code here ─────────────
  //   read sensors / control relays / your logic
  // ───────────────────────────────────────────────────

  vTaskDelay(pdMS_TO_TICKS(1000));
}
