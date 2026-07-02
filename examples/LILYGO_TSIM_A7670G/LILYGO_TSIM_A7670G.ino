/*
  ================================================================
   LILYGO_TSIM_A7670G — APIOTA Library Example
   For: LILYGO TTGO T-SIM A7670G (ESP32 + SIMCom A7670G LTE Cat-1)
   ================================================================

   Board: LILYGO T-SIM A7670G
   Modem: SIMCom A7670G — LTE Cat-1 / GSM / GPRS
   Network: 4G LTE Cat-1 (much better than SIM800L, native SSL/TLS)

   Pin Mapping (T-SIM A7670G):
     Modem UART TX  → ESP32 GPIO 26  (Serial1 RX)
     Modem UART RX  → ESP32 GPIO 27  (Serial1 TX)
     PWRKEY         → ESP32 GPIO 4
     POWER_PIN      → ESP32 GPIO 25  (enable modem power supply)
     STATUS_PIN     → ESP32 GPIO 23  (modem status output)
     RI (Ring Ind.) → ESP32 GPIO 33  (optional)

   Dependencies (install via Library Manager):
     - TinyGSM (vshymanskyy) — v0.12.0+ supports A7670

   Notes:
     - A7670G supports native SSL/TLS → use TinyGsmClientSecure directly
     - LTE Cat-1: downloads firmware ~10x faster than SIM800L GPRS
     - no special voltage concerns (board has a complete power circuit)
  ================================================================
*/

// ╔══════════════════════════════════════════════════════════════╗
//   DEBUG TOGGLE — on while testing / off in production
//     1 = test mode: show raw AT + verbose log for every step
//     0 = production: quiet (only work/OTA status), stable, faster
// ╚══════════════════════════════════════════════════════════════╝
#define DEBUG_MODE  1

// ── MODEM SELECTION ─────────────────────────────────────────────
#define TINY_GSM_MODEM_A7672X   // A7670G: uses the A7672X driver (has TinyGsmClientSecure)
#define TINY_GSM_YIELD_MS  1    // driver yields 1ms while waiting for AT → prevents Task Watchdog reset during SSL read
#if DEBUG_MODE
#define TINY_GSM_DEBUG  Serial
#endif
// ─────────────────────────────────────────────────────────────────

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <Preferences.h>
#include <Update.h>
#include "mbedtls/sha256.h"
#include "mbedtls/pk.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "esp_system.h"

// ── LOG MACROS ──────────────────────────────────────────────────
//   LOGD = verbose log (DEBUG_MODE only) | LOG = main output (always shown)
#if DEBUG_MODE
#define LOGD(...)   Serial.printf(__VA_ARGS__)
#define LOGDLN(x)   Serial.println(x)
#else
#define LOGD(...)   do{}while(0)
#define LOGDLN(x)   do{}while(0)
#endif
#define LOG(...)      Serial.printf(__VA_ARGS__)
#define LOGLN(x)      Serial.println(x)

// ╔══════════════════════════════════════════════════════════════════╗
//   USER CONFIG — edit only this section
// ╠══════════════════════════════════════════════════════════════════╣
#define API_KEY           "ak_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // from apiota.net Dashboard
#define CURRENT_VERSION   "1.0.0"                // bump on every new build
#define DEVICE_NAME       "TSIM_A7670G"          // shown in Dashboard → Devices (syncs on reflash, v1.4.1)
#define OTA_SERVER        "apiota.net"
#define OTA_SERVER_PORT   443                    // HTTPS

// ── APN per your network operator ──
#define APN               "www.dtac.co.th" // DTAC (per SIM) | alt: www.dtac.net | AIS="internet" | TRUE="internet.true.th"
#define GPRS_USER         ""
#define GPRS_PASS         ""

#define OTA_CHECK_SEC     300   // check OTA every 5 minutes
#define TELEMETRY_SEC     10    // send values to the server (Monitor) every 10s — GPS/battery/etc.
// ╚══════════════════════════════════════════════════════════════════╝

// ── PIN CONFIG (LILYGO T-A7670, ESP32-WROVER-E) ──────────────────
#define MODEM_RX_PIN      27   // ESP RX ← modem TX
#define MODEM_TX_PIN      26   // ESP TX → modem RX
#define MODEM_PWRKEY_PIN   4
#define MODEM_POWER_PIN   12   // BOARD_POWERON: HIGH = power the modem
#define MODEM_RESET_PIN    5   // RESET modem (active HIGH on T-A7670)
#define MODEM_BAUD        115200
#define LED_PIN           32   // status LED (cosmetic; avoid GPIO12 = POWERON)
#define BAT_ADC_PIN       35   // read 18650 battery voltage (board has a 2:1 voltage divider)
#define BAT_LOW_VOLT      3.40 // below this = battery almost empty (warn / reduce work)

// ── PUBLIC KEY ───────────────────────────────────────────────────
static const char* APIOTA_PUBKEY = R"KEY(
-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA3WAvuqxM4mR1k9UiH6r3
0/pClZy0v1PisQ5kFSaJt8+BWsa1YBbanJsoGDG54y5gqnB+eiyqQTeKEA5fJlAH
lLOr72RA+vGkMfRmmZXO+EOaGKQoK6SvB/tV2rvBSNxkZeqPXn6PzdvFOaZc6ej+
fmjwsBssHGLifXLFrie3AEHY9N5DpAOf784ZsxXMJx85I7XlxnD/igDBa68O9Z9t
p8V/YRIRZ9xM7ZxNBgbnmaDJ8nNbDVScujS/HtBh90o37dZg20zSKIghp0QEfC1g
JZ92a/llA+HhpWi+5ysigP5JlHVBiIn8e48bdhx3w026IyaeBnaCJANtKmvlQroZ
DQIDAQAB
-----END PUBLIC KEY-----
)KEY";

// ── GLOBAL OBJECTS ────────────────────────────────────────────────
HardwareSerial modemSerial(1);          // UART1
#if DEBUG_MODE
// ── AT debug dumper: echo modem TX/RX to Serial (test mode only) ──
struct ATDump : public Stream {
  Stream& up; ATDump(Stream& u) : up(u) {}
  int available() override { return up.available(); }
  int read() override { int c = up.read(); if (c >= 0) Serial.write((uint8_t)c); return c; }
  int peek() override { return up.peek(); }
  size_t write(uint8_t c) override { Serial.write(c); return up.write(c); }
  void flush() override { up.flush(); }
};
ATDump         atStream(modemSerial);
TinyGsm        modem(atStream);
#else
TinyGsm        modem(modemSerial);        // production: direct, no echo (more stable)
#endif
TinyGsmClientSecure gsmClient(modem, 0);  // MuxChannel 0 — API calls
// Channel 1 is created during firmware download to separate the connection

static char    g_deviceId[20]     = {0};
static char    g_deviceSecret[80] = {0};
static bool    g_provisioned      = false;
static uint32_t g_lastCheckMs     = 0;
static uint32_t g_lastTeleMs      = 0;       // last telemetry send (GPS/values)
static uint32_t g_appliedSeq      = 0;       // B: last applied rollout seq (stored in NVS) — prevents flap on deploy/rollback
static volatile bool g_devReady   = true;    // v1.4.1: false = pending ✓ Approve / locked — gate your loop() with this
static volatile float g_vbat      = 0.0f;    // latest battery voltage (loop updates, otaTask sends to server)
static bool    g_ledState         = false;

// ── CHIP ID ──────────────────────────────────────────────────────
// ── OTA info struct (placed before the first function to avoid Arduino auto-prototype errors) ──
struct ApiotaOTA { bool available; String version; String url; String sha256; String signature; uint32_t rolloutSeq; };

static uint32_t calcChipId() {
  uint32_t id = 0;
  for (int i = 0; i < 17; i += 8) id |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  return id;
}

// ── LED BLINK ────────────────────────────────────────────────────
static void ledBlink(int times, int ms = 200) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH); delay(ms);
    digitalWrite(LED_PIN, LOW);  delay(ms);
  }
}

// ── JSON HELPER ──────────────────────────────────────────────────
static String jget(const String& js, const char* key) {
  String sk = String('"') + key + "\":";
  int s = js.indexOf(sk);
  if (s < 0) return "";
  s += sk.length();
  while (s < (int)js.length() && (js[s]==' '||js[s]=='\n'||js[s]=='\t')) s++;
  if (s >= (int)js.length()) return "";
  char c = js[s];
  if (c == '"') {
    int e = s + 1;
    while (e < (int)js.length()) {
      if (js[e] == '\\') { e += 2; continue; }
      if (js[e] == '"') break;
      e++;
    }
    return js.substring(s + 1, e);
  }
  int e = s;
  while (e < (int)js.length() && js[e] != ',' && js[e] != '}' && js[e] != ']') e++;
  String v = js.substring(s, e); v.trim(); return v;
}

static String urlEncode(const String& s) {
  String out;
  for (char c : s) {
    if (isAlphaNumeric(c) || c=='-'||c=='_'||c=='.'||c=='~') out += c;
    else { char b[4]; snprintf(b, sizeof(b), "%%%02X", (uint8_t)c); out += b; }
  }
  return out;
}

// ── NVS ──────────────────────────────────────────────────────────
static void loadCredentials() {
  Preferences p; p.begin("apiota", true);
  String id  = p.getString("dev_id", "");
  String sec = p.getString("dev_sec", "");
  g_appliedSeq = p.getULong("seq", 0);
  p.end();
  if (id == g_deviceId && sec.length() > 0) {
    strncpy(g_deviceSecret, sec.c_str(), sizeof(g_deviceSecret) - 1);
    g_provisioned = true;
    LOGD("[NVS] credentials loaded for %s\n", g_deviceId);
  }
}

static void saveCredentials(const char* secret) {
  Preferences p; p.begin("apiota", false);
  p.putString("dev_id", g_deviceId);
  p.putString("dev_sec", secret);
  p.end();
}

static void clearCredentials() {
  Preferences p; p.begin("apiota", false);
  p.remove("dev_id"); p.remove("dev_sec"); p.end();
  g_deviceSecret[0] = '\0'; g_provisioned = false;
}

// ── MODEM POWER ON ───────────────────────────────────────────────
//   A7670G power sequence per the SIMCom datasheet
// ─────────────────────────────────────────────────────────────────
static void modemPowerOn() {
  // 1) POWERON (GPIO12) powers the modem
  pinMode(MODEM_POWER_PIN, OUTPUT);
  digitalWrite(MODEM_POWER_PIN, HIGH);
  delay(100);

  // open UART + check whether the modem is already on (ESP rebooted but modem still powered)
  // if already on → skip PWRKEY (otherwise the pulse toggles the modem off → AT stops responding)
  modemSerial.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
  delay(200);
  if (modem.testAT(1500)) { LOGDLN("[GSM] modem already on — skip power-on"); return; }

  // 2) RESET modem (T-A7670: active HIGH) — per known-working code
  pinMode(MODEM_RESET_PIN, OUTPUT);
  digitalWrite(MODEM_RESET_PIN, LOW);  delay(100);
  digitalWrite(MODEM_RESET_PIN, HIGH); delay(1000);
  digitalWrite(MODEM_RESET_PIN, LOW);

  // 3) PWRKEY pulse — A7670 needs HIGH for 1000ms (the old 100ms was too short, modem wouldn't wake!)
  pinMode(MODEM_PWRKEY_PIN, OUTPUT);
  digitalWrite(MODEM_PWRKEY_PIN, LOW);  delay(100);
  digitalWrite(MODEM_PWRKEY_PIN, HIGH); delay(1000);
  digitalWrite(MODEM_PWRKEY_PIN, LOW);

  // don't poll STATUS — actual readiness is checked with testAT() in modemInit()
  LOGDLN("[GSM] power-on (POWERON+RESET+PWRKEY) done, modem booting...");
  delay(5000);   // A7670 boots slowly, wait ~5s before AT
}

static void modemPowerOff() {
  // Toggle PWRKEY to power off
  digitalWrite(MODEM_PWRKEY_PIN, HIGH);
  delay(1500);
  digitalWrite(MODEM_PWRKEY_PIN, LOW);
  delay(3000);
  digitalWrite(MODEM_POWER_PIN, LOW);
}

// ── RAW HTTPS REQUEST (manual TCP + HTTP/1.1) ────────────────────
static int gsmHttpRequest(const String& method, const String& path,
                          const String& postBody, String& outBody,
                          TinyGsmClientSecure& client) {
  outBody = "";

  if (!client.connect(OTA_SERVER, OTA_SERVER_PORT)) {
    LOGDLN("[HTTP] connect failed"); return -1;
  }

  // Headers
  String req = method + " " + path + " HTTP/1.1\r\n";
  req += "Host: "            + String(OTA_SERVER)       + "\r\n";
  req += "X-API-Key: "       + String(API_KEY)           + "\r\n";
  req += "X-Device-ID: "     + String(g_deviceId)        + "\r\n";
  if (g_deviceSecret[0])
    req += "X-Device-Secret: " + String(g_deviceSecret)  + "\r\n";
  req += "X-Chip-ID: "       + String(calcChipId())      + "\r\n";
  req += "Connection: close\r\n";
  if (method == "POST" && postBody.length() > 0) {
    req += "Content-Type: application/json\r\n";
    req += "Content-Length: " + String(postBody.length()) + "\r\n";
  }
  req += "\r\n";
  if (method == "POST") req += postBody;

  client.print(req);

  // Read status + headers
  uint32_t t0 = millis();
  int statusCode = -1;
  bool inBody = false;
  String statusLine;

  while ((client.connected() || client.available()) && millis() - t0 < 20000) {
    if (!client.available()) { delay(5); continue; }
    String line = client.readStringUntil('\n');
    line.trim();
    if (statusCode < 0 && line.startsWith("HTTP/")) {
      // "HTTP/1.1 200 OK"
      statusCode = line.substring(9, 12).toInt();
    }
    if (line.length() == 0) { inBody = true; break; }  // blank line = body start
  }
  // Read body
  t0 = millis();
  while ((client.connected() || client.available()) && millis() - t0 < 15000) {
    if (client.available()) { outBody += (char)client.read(); }
    else delay(2);
  }
  client.stop();
  // fully close the CCH SSL service so the next connect can set CCHSET/CCHSTART again
  // (otherwise the 2nd request ERRORs at CCHSET because the service is still running)
  modem.sendAT(GF("+CCHSTOP"));
  modem.waitResponse(2000L);
  return statusCode;
}

// ── PROVISION ────────────────────────────────────────────────────
static bool doProvision() {
  LOGDLN("[APIOTA] provisioning...");
  uint32_t chipId = calcChipId();
  char hexId[16]; snprintf(hexId, sizeof(hexId), "0x%06X", (unsigned)chipId);

  String body = String("{") +
    "\"api_key\":\"" + API_KEY + "\"," +
    "\"device_id\":\"" + g_deviceId + "\"," +
    "\"device_name\":\"" + DEVICE_NAME + "\"," +
    "\"version\":\"" + CURRENT_VERSION + "\"," +
    "\"chip_info\":{\"chip_id\":" + String(chipId) + ",\"chip_id_hex\":\"" + hexId + "\"}" +
    "}";

  String resp;
  int code = gsmHttpRequest("POST", "/api/device/provision", body, resp, gsmClient);
  LOGD("[APIOTA] provision HTTP %d\n", code);

  if (code == 200 || code == 201) {
    String secret = jget(resp, "device_secret");
    if (!secret.length()) { LOGDLN("[APIOTA] no secret in response"); return false; }
    strncpy(g_deviceSecret, secret.c_str(), sizeof(g_deviceSecret) - 1);
    saveCredentials(g_deviceSecret);
    g_provisioned = true;
    g_devReady = (jget(resp, "requires_approval") != "true");   // v1.4.1: pending → hold until ✓ Approve
    LOG("[APIOTA] registered — waiting for Approve in Dashboard\n");
    return true;
  }
  if (code == 402 || code == 403) {
    LOG("[APIOTA] device plan limit — upgrade at apiota.net\n");
  }
  return false;
}

// ── CHECK UPDATE ─────────────────────────────────────────────────

static ApiotaOTA checkUpdate() {
  ApiotaOTA inf = {};
  // B: send applied_seq → server decides from the rollout counter (flap-free deploy/rollback)
  String path = "/api/device/check-update?version=" + urlEncode(CURRENT_VERSION) + "&seq=" + String(g_appliedSeq) + "&name=" + urlEncode(DEVICE_NAME);   // v1.4.1: name sync
  String resp;
  int code = gsmHttpRequest("GET", path, "", resp, gsmClient);
  if (code == 404) {
    // device was deleted on the server → clear NVS and re-provision (self-heal)
    LOGDLN("[APIOTA] device not found (404) — clearing NVS + re-provision");
    clearCredentials();
    if (doProvision()) LOG("[APIOTA] re-provisioned — waiting for Approve in Dashboard\n");
    return inf;
  }
  // v1.4.1: Approval / Lock gate — pending/locked → g_devReady=false (loop() ที่ gate ไว้จะหยุดรอ)
  if (code == 403 && resp.indexOf("device_pending_approval") >= 0) { g_devReady = false; LOGDLN("[APIOTA] waiting for owner approval"); return inf; }
  if (code == 403 && resp.indexOf("device_locked") >= 0)           { g_devReady = false; LOGDLN("[APIOTA] device LOCKED by owner");    return inf; }
  if (code != 200) { LOGD("[APIOTA] checkUpdate HTTP %d\n", code); return inf; }
  g_devReady = true;   // approved + unlocked
  inf.available  = (jget(resp, "update") == "true");
  inf.rolloutSeq = (uint32_t)jget(resp, "rollout_seq").toInt();   // B: this rollout's seq
  if (inf.available) {
    inf.version   = jget(resp, "version");
    inf.url       = jget(resp, "url");
    inf.sha256    = jget(resp, "sha256"); inf.sha256.toLowerCase();
    inf.signature = jget(resp, "signature");
    LOG("[OTA] update available: %s → %s\n", CURRENT_VERSION, inf.version.c_str());
  } else {
    LOGD("[APIOTA] up to date (%s)\n", CURRENT_VERSION);
  }
  return inf;
}

// ── SIGNATURE VERIFY ─────────────────────────────────────────────
static bool verifySig(const String& sha256Hex, const String& sigB64) {
  if (!sha256Hex.length() || !sigB64.length()) return false;
  uint8_t sig[512]; size_t slen = 0;
  if (mbedtls_base64_decode(sig, sizeof(sig), &slen,
      (const uint8_t*)sigB64.c_str(), sigB64.length()) != 0) return false;
  uint8_t digest[32];
  mbedtls_md_context_t ctx; mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, (const uint8_t*)sha256Hex.c_str(), sha256Hex.length());
  mbedtls_md_finish(&ctx, digest);
  mbedtls_md_free(&ctx);
  mbedtls_pk_context pk; mbedtls_pk_init(&pk);
  if (mbedtls_pk_parse_public_key(&pk, (const uint8_t*)APIOTA_PUBKEY,
      strlen(APIOTA_PUBKEY) + 1) != 0) { mbedtls_pk_free(&pk); return false; }
  int r = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest, sizeof(digest), sig, slen);
  mbedtls_pk_free(&pk);
  return (r == 0);
}

// ── OTA DOWNLOAD + FLASH ─────────────────────────────────────────
static bool performOTA(const ApiotaOTA& inf) {
  Serial.println("[OTA] verifying signature...");
  if (!verifySig(inf.sha256, inf.signature)) {
    Serial.println("[OTA] ✗ signature FAILED — abort"); ledBlink(5, 100); return false;
  }
  Serial.println("[OTA] ✓ signature OK — downloading...");
  ledBlink(2, 300);

  // Parse URL → host + port + path
  String url = inf.url;
  bool isHttps = url.startsWith("https://");
  url.replace("https://", ""); url.replace("http://", "");
  int slashIdx = url.indexOf('/');
  String host = (slashIdx >= 0) ? url.substring(0, slashIdx) : url;
  String dlPath = (slashIdx >= 0) ? url.substring(slashIdx) : "/";
  uint16_t port = isHttps ? 443 : 80;
  int colonIdx = host.indexOf(':');
  if (colonIdx >= 0) { port = host.substring(colonIdx + 1).toInt(); host = host.substring(0, colonIdx); }

  // Use MuxChannel 1 for download (separate from the API client)
  TinyGsmClientSecure dlClient(modem, 1);
  if (!dlClient.connect(host.c_str(), port)) {
    Serial.println("[OTA] download connect failed"); return false;
  }

  String req = "GET " + dlPath + " HTTP/1.1\r\n";
  req += "Host: " + host + "\r\n";
  // B2: device auth headers when downloading .bin (server added deviceAuth at the download endpoint in phase 2)
  req += "X-Device-ID: "     + String(g_deviceId)     + "\r\n";
  if (g_deviceSecret[0])
    req += "X-Device-Secret: " + String(g_deviceSecret) + "\r\n";
  req += "X-Chip-ID: "       + String(calcChipId())   + "\r\n";
  req += "Connection: close\r\n\r\n";
  dlClient.print(req);

  // Skip headers + parse Content-Length
  int contentLength = -1;
  uint32_t t0 = millis();
  while ((dlClient.connected() || dlClient.available()) && millis() - t0 < 15000) {
    if (!dlClient.available()) { delay(5); continue; }
    String line = dlClient.readStringUntil('\n'); line.trim();
    if (line.startsWith("Content-Length:")) contentLength = line.substring(15).toInt();
    if (line.length() == 0) break;
  }

  if (contentLength <= 0) {
    Serial.println("[OTA] unknown firmware size"); dlClient.stop(); return false;
  }
  Serial.printf("[OTA] firmware size: %d bytes\n", contentLength);

  if (!Update.begin(contentLength)) {
    Serial.println("[OTA] no flash space"); dlClient.stop(); return false;
  }

  // Stream → flash + SHA256
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha); mbedtls_sha256_starts(&sha, 0);

  uint8_t buf[1024];   // A7670G LTE → can use a large buffer (much faster than SIM800L)
  int written = 0;
  uint32_t lastLog = 0, lastData = millis();

  while ((dlClient.connected() || dlClient.available()) && written < contentLength) {
    int avail = dlClient.available();
    if (!avail) {
      if (millis() - lastData > 20000) {
        Serial.println("[OTA] download timeout"); Update.abort(); dlClient.stop(); return false;
      }
      delay(5);
      continue;
    }
    int rd = dlClient.readBytes(buf, min(avail, (int)sizeof(buf)));
    if (rd <= 0) { delay(2); continue; }
    lastData = millis();
    mbedtls_sha256_update(&sha, buf, rd);
    if (Update.write(buf, rd) != (size_t)rd) {
      Serial.println("[OTA] flash write error"); Update.abort(); dlClient.stop(); return false;
    }
    written += rd;
    // LED blink during download
    if (millis() - lastLog >= 2000) {
      lastLog = millis();
      int pct = (int)((int64_t)written * 100 / contentLength);
      Serial.printf("[OTA] %d/%d bytes  %d%%\n", written, contentLength, pct);
      g_ledState = !g_ledState; digitalWrite(LED_PIN, g_ledState);
    }
  }
  dlClient.stop();
  digitalWrite(LED_PIN, LOW);

  // Verify SHA256
  uint8_t hash[32]; mbedtls_sha256_finish(&sha, hash); mbedtls_sha256_free(&sha);
  char calc[65];
  for (int i = 0; i < 32; i++) sprintf(calc + i * 2, "%02x", hash[i]);
  calc[64] = '\0';

  if (inf.sha256.length() && inf.sha256 != String(calc)) {
    Serial.printf("[OTA] ✗ SHA256 mismatch\n  expected: %.32s...\n  got:      %.32s...\n",
                  inf.sha256.c_str(), calc);
    Update.abort(); return false;
  }
  Serial.println("[OTA] ✓ SHA256 OK");

  if (!Update.end(true) || !Update.isFinished()) {
    Serial.println("[OTA] Update.end() failed"); return false;
  }

  ledBlink(3, 200);
  // B: save the rollout seq just applied → next round won't re-pull the same rollout (flap-free)
  { Preferences p; p.begin("apiota", false); p.putULong("seq", inf.rolloutSeq); p.end(); }
  Serial.println("[OTA] ✓ SUCCESS — rebooting in 3s...");
  delay(3000);
  ESP.restart();
  return true;
}

// ── LTE CONNECT ──────────────────────────────────────────────────
static bool lteConnect() {
  LOGD("[LTE] connecting (APN: %s)...\n", APN);
  // the A7672X driver ends gprsConnect with +CDNSCFG, which A7670 (LTE) doesn't support → returns false
  // even though the context is up, so check the real IP/CGATT instead of the return value
  modem.gprsConnect(APN, GPRS_USER, GPRS_PASS);   // set up the PDP context (CGATT/CGPADDR)
  // wait for the network — retry to handle temporary bad CREG replies (don't fail/restart immediately)
  for (int i = 0; i < 12; i++) {
    if (modem.isNetworkConnected()) {
      int csq = modem.getSignalQuality();   // 0-31, 99=unknown; 10+=usable
      LOGD("[LTE] connected  RSSI: %d (%s)\n", csq,
        csq == 99 ? "unknown" : (csq >= 15 ? "good" : (csq >= 10 ? "fair" : "poor")));
      return true;
    }
    delay(1000);
  }
  LOGDLN("[LTE] connect failed (not registered)");
  return false;
}

// ── MODEM INIT ───────────────────────────────────────────────────
static bool modemInit() {
  modemPowerOn();
  modemSerial.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
  delay(500);

  LOGD("[GSM] AT test");
  for (int i = 0; i < 20; i++) {
    if (modem.testAT(1000)) { LOGDLN(" OK"); break; }
    LOGD(".");
    if (i == 19) { LOGDLN(" AT failed!"); return false; }
  }

  modem.sendAT(GF("+CMEE=2"));  // verbose error messages
  modem.waitResponse();

  LOGD("[GSM] model: %s\n", modem.getModemName().c_str());
  LOGD("[GSM] IMEI:  %s\n", modem.getIMEI().c_str());
  LOGD("[GSM] ICCID: %s\n", modem.getSimCCID().c_str());

  // wait for the SIM to be ready (up to 30s)
  LOGD("[GSM] waiting SIM");
  for (int i = 0; i < 15; i++) {
    SimStatus s = modem.getSimStatus();
    if (s == SIM_READY) { LOGDLN(" ready"); break; }
    LOGD(".");
    delay(2000);
    if (i == 14) { LOGDLN(" SIM not ready!"); return false; }
  }

  // wait for network registration (up to 60s)
  LOGD("[GSM] registering");
  for (int i = 0; i < 30; i++) {
    if (modem.isNetworkConnected()) {
      LOGD(" OK (operator: %s)\n", modem.getOperator().c_str());
      break;
    }
    LOGD(".");
    delay(2000);
    if (i == 29) { LOGDLN(" network failed!"); return false; }
  }

  // SSL: authmode=0 = don't verify the server cert (A7670 has no CA + clock not synced → verify fails)
  // if not set, the modem verifies apiota.net's cert and fails -> CCHOPEN/connect failed
  modem.sendAT(GF("+CSSLCFG=\"authmode\",0,0"));
  modem.waitResponse();

  return true;
}

// ================================================================
//  GNSS + TELEMETRY — device sends values to the server (📡 Monitor button)
//    sends GPS (if a fix is available) + generic values in data (e.g. battery/temperature/status)
// ================================================================
// turn on GNSS (best-effort; if already on it replies ERROR, which is fine to skip)
static void gnssOn() {
  modem.sendAT(GF("+CGNSSPWR=1"));
  modem.waitResponse(10000L);                  // OK
  modem.waitResponse(12000L, GF("READY!"));    // wait for the GNSS engine (URC) — best effort
  LOGD("[GNSS] powered on (acquiring satellites, first fix may take 30s-2min)\n");
}

// read coordinates from AT+CGNSSINFO → true if there is a fix
//   format: +CGNSSINFO: <mode>,<GPSsv>,<GLONASSsv>,<BEIDOUsv>,<lat>,<N/S>,<lon>,<E/W>,<date>,<utc>,<alt>,<speed>,<course>,...
static bool readGNSS(double& lat, double& lon, double& alt, double& spd, int& sats) {
  String data;
  modem.sendAT(GF("+CGNSSINFO"));
  if (modem.waitResponse(3000L, data) != 1) return false;
  int p = data.indexOf("+CGNSSINFO:");
  if (p < 0) return false;
  String s = data.substring(p + 11);
  int nl = s.indexOf('\r'); if (nl >= 0) s = s.substring(0, nl);
  s.trim();
  String tok[17]; int n = 0, start = 0;
  for (int i = 0; i <= (int)s.length() && n < 17; i++) {
    if (i == (int)s.length() || s[i] == ',') { tok[n++] = s.substring(start, i); start = i + 1; }
  }
  if (n < 8 || tok[4].length() == 0 || tok[6].length() == 0) return false;   // no fix yet
  double rawLat = tok[4].toDouble();    // ddmm.mmmmmm
  double rawLon = tok[6].toDouble();    // dddmm.mmmmmm
  double dLat = floor(rawLat / 100.0), mLat = rawLat - dLat * 100.0; lat = dLat + mLat / 60.0;
  double dLon = floor(rawLon / 100.0), mLon = rawLon - dLon * 100.0; lon = dLon + mLon / 60.0;
  if (tok[5] == "S") lat = -lat;
  if (tok[7] == "W") lon = -lon;
  sats = tok[1].toInt() + tok[2].toInt() + tok[3].toInt();
  alt  = (n > 10) ? tok[10].toDouble() : 0.0;
  spd  = (n > 11) ? tok[11].toDouble() : 0.0;
  return true;
}

// send telemetry to the server: GPS (if any) + generic data
//   to add other values (temp/humid/status) → put them in dataBuf as JSON
static void sendTelemetry() {
  double lat = 0, lon = 0, alt = 0, spd = 0; int sats = 0;
  bool fix = readGNSS(lat, lon, alt, spd, sats);

  char dataBuf[96];
  if (g_vbat >= 2.5f) snprintf(dataBuf, sizeof(dataBuf), "{\"batt_v\":%.2f}", g_vbat);
  else                snprintf(dataBuf, sizeof(dataBuf), "{\"power\":\"usb\"}");

  String body = "{";
  if (fix) {
    char g[160];
    snprintf(g, sizeof(g), "\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.1f,\"speed\":%.1f,\"sats\":%d,",
             lat, lon, alt, spd, sats);
    body += g;
  }
  body += "\"data\":"; body += dataBuf; body += "}";

  String resp;
  int code = gsmHttpRequest("POST", "/api/device/telemetry", body, resp, gsmClient);
  if (fix) LOGD("[TELE] GPS %.6f,%.6f (%d sats) -> HTTP %d\n", lat, lon, sats, code);
  else     LOGD("[TELE] no GPS fix, sent data only -> HTTP %d\n", code);
}

// ================================================================
//  OTA TASK (FreeRTOS) — modem/network/OTA work, separate from the main loop
// ================================================================
static void otaTask(void* pv) {
  // init modem + LTE — on failure, restart for a clean retry (more stable than hanging)
  if (!modemInit())  { LOGLN("FATAL: modem init failed — restart"); ledBlink(10,100); vTaskDelay(pdMS_TO_TICKS(2000)); ESP.restart(); }
  if (!lteConnect()) { LOGLN("FATAL: LTE connect failed — restart"); ledBlink(5,200);  vTaskDelay(pdMS_TO_TICKS(2000)); ESP.restart(); }
  ledBlink(1, 300);
  gnssOn();   // turn on GPS (acquires a fix in the background — values arrive as satellites are seen)

  // provision (if there's no credential yet)
  loadCredentials();
  if (!g_provisioned) {
    for (int i = 0; i < 3 && !g_provisioned; i++) {
      if (doProvision()) break;
      LOGD("[APIOTA] provision attempt %d/3 failed\n", i + 1);
      vTaskDelay(pdMS_TO_TICKS(5000));
    }
    if (!g_provisioned) { LOGLN("FATAL: provision failed — restart"); ledBlink(10,100); vTaskDelay(pdMS_TO_TICKS(2000)); ESP.restart(); }
  }

  // OTA check immediately after boot
  { ApiotaOTA inf = checkUpdate(); if (inf.available) performOTA(inf); }
  digitalWrite(LED_PIN, HIGH);   // LED on = running normally
  LOG("=== APIOTA ready (device %s) ===\n", g_deviceId);

  // OTA work loop: reconnect if dropped + check OTA on the interval
  for (;;) {
    if (!modem.isNetworkConnected()) {   // CREG — reliable (doesn't rely on CCHADDR)
      LOGDLN("[LTE] disconnected — reconnecting...");
      digitalWrite(LED_PIN, LOW);
      lteConnect();
      digitalWrite(LED_PIN, HIGH);
    }
    uint32_t now = millis();
    // send telemetry (GPS/battery) to the server every TELEMETRY_SEC → view it in the 📡 Monitor button
    if (now - g_lastTeleMs >= (uint32_t)TELEMETRY_SEC * 1000UL) {
      g_lastTeleMs = now;
      if (modem.isNetworkConnected()) sendTelemetry();
    }
    if (now - g_lastCheckMs >= (uint32_t)OTA_CHECK_SEC * 1000UL) {
      g_lastCheckMs = now;
      LOGD("[APIOTA] check (signal: %d, heap: %dKB)\n",
           modem.getSignalQuality(), ESP.getFreeHeap() / 1024);
      ApiotaOTA inf = checkUpdate();
      if (inf.available) performOTA(inf);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));   // yield — don't block other tasks
  }
}

// ================================================================
//  SETUP — create the OTA task and leave loop() for your application code
// ================================================================
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  uint32_t chipId = calcChipId();
  snprintf(g_deviceId, sizeof(g_deviceId), "ESP32_%06X", (unsigned)chipId);
  LOG("\n=== APIOTA T-A7670G  v%s  Device: %s ===\n", CURRENT_VERSION, g_deviceId);

  // modem/OTA runs in a separate task (core 0, 16KB stack for OTA+verify)
  // loop() runs free on core 1 — put your application code there, not blocked by OTA
  xTaskCreatePinnedToCore(otaTask, "apiota_ota", 16384, NULL, 2, NULL, 0);
}

// ================================================================
//  BATTERY — read 18650 battery voltage (GPIO35 + 2:1 voltage divider)
// ================================================================
static volatile bool g_batteryLow = false;   // otaTask can check this (e.g. pause OTA on low battery)

static float readBatteryVoltage() {
  uint16_t s[15];
  for (int i = 0; i < 15; i++) { s[i] = analogReadMilliVolts(BAT_ADC_PIN); delayMicroseconds(300); }
  for (int i = 0; i < 15; i++)                  // sort to find the median (reduce noise)
    for (int j = i + 1; j < 15; j++)
      if (s[j] < s[i]) { uint16_t t = s[i]; s[i] = s[j]; s[j] = t; }
  return (s[7] * 2.0f) / 1000.0f;               // median × 2 (divider) / 1000
}

static int batteryPercent(float v) {          // approximate Li-ion
  if (v >= 4.20f) return 100;
  if (v <= 3.30f) return 0;
  return (int)((v - 3.30f) / (4.20f - 3.30f) * 100.0f);
}

// ================================================================
//  LOOP — your application code (free, not blocked by network/OTA)
// ================================================================
void loop() {
  // ── Approval / Lock gate (v1.4.1) — otaTask ยังเช็ค server ต่อ ปลดเองเมื่อ ✓ Approve / Unlock ──
  static bool s_wasReady = true;
  if (!g_devReady) {
    s_wasReady = false;
    digitalWrite(LED_PIN, (millis() / 150) % 2);   // fast blink = waiting
    vTaskDelay(pdMS_TO_TICKS(100));
    return;                                        // application code does not run yet
  }
  if (!s_wasReady) { s_wasReady = true; digitalWrite(LED_PIN, HIGH); LOG("[APIOTA] approved/unlocked - resuming\n"); }

  static uint32_t lastBat = 0;
  if (millis() - lastBat >= 10000) {          // read the battery every 10 seconds
    lastBat = millis();
    float vbat = readBatteryVoltage();
    g_vbat = vbat;                              // store for otaTask to send to the server (Monitor)
    if (vbat < 2.5f) {                          // < 2.5V = no battery on the rail (switch OFF / powered via USB)
      g_batteryLow = false;
      LOG("[BAT] USB power (no battery / switch OFF)\n");
    } else {
      int pct = batteryPercent(vbat);
      g_batteryLow = (vbat < BAT_LOW_VOLT);
      LOG("[BAT] %.2f V  (~%d%%)%s\n", vbat, pct, g_batteryLow ? "  ⚠ LOW" : "");
    }
  }

  // ───────────── put your own code here ─────────────
  //   read sensors / control relays etc. — OTA+4G is handled in otaTask
  // ───────────────────────────────────────────────────
  vTaskDelay(pdMS_TO_TICKS(1000));
}
