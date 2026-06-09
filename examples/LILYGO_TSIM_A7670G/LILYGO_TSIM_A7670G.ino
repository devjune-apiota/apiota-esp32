/*
  ================================================================
   LILYGO_TSIM_A7670G — APIOTA Library Example
   สำหรับ: LILYGO TTGO T-SIM A7670G (ESP32 + SIMCom A7670G LTE Cat-1)
   ================================================================

   Board: LILYGO T-SIM A7670G
   Modem: SIMCom A7670G — LTE Cat-1 / GSM / GPRS
   Network: 4G LTE Cat-1 (ดีกว่า SIM800L มาก, SSL/TLS native)

   Pin Mapping (T-SIM A7670G):
     Modem UART TX  → ESP32 GPIO 26  (Serial1 RX)
     Modem UART RX  → ESP32 GPIO 27  (Serial1 TX)
     PWRKEY         → ESP32 GPIO 4
     POWER_PIN      → ESP32 GPIO 25  (enable modem power supply)
     STATUS_PIN     → ESP32 GPIO 23  (modem status output)
     RI (Ring Ind.) → ESP32 GPIO 33  (optional)

   Dependencies (ติดตั้งใน Library Manager):
     - TinyGSM (vshymanskyy) — v0.12.0+ รองรับ A7670

   Notes:
     - A7670G รองรับ SSL/TLS native → ใช้ TinyGsmClientSecure ได้เลย
     - LTE Cat-1: ดาวน์โหลด firmware เร็วกว่า SIM800L GPRS ~10x
     - ไม่ต้องระวังเรื่อง voltage พิเศษ (board มี power circuit ครบ)
  ================================================================
*/

// ╔══════════════════════════════════════════════════════════════╗
//   DEBUG TOGGLE — เปิดตอนเทส / ปิดตอนใช้งานจริง
//     1 = โหมดเทส: โชว์ AT ดิบ + log ละเอียดทุก step
//     0 = production: เงียบ (โชว์เฉพาะเนื้องาน/สถานะ OTA), เสถียร, เร็วกว่า
// ╚══════════════════════════════════════════════════════════════╝
#define DEBUG_MODE  1

// ── MODEM SELECTION ─────────────────────────────────────────────
#define TINY_GSM_MODEM_A7672X   // A7670G: driver A7672X (มี TinyGsmClientSecure)
#define TINY_GSM_YIELD_MS  1    // driver yield 1ms ตอนรอ AT → กัน Task Watchdog reset ระหว่าง SSL read
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
//   LOGD = log ละเอียด (เฉพาะ DEBUG_MODE) | LOG = เนื้องานหลัก (โชว์เสมอ)
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
//   USER CONFIG — แก้ไขเฉพาะส่วนนี้
// ╠══════════════════════════════════════════════════════════════════╣
#define API_KEY           "ak_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // จาก apiota.net Dashboard
#define CURRENT_VERSION   "1.0.0"                // เพิ่มทุกครั้งที่ build ใหม่
#define OTA_SERVER        "apiota.net"
#define OTA_SERVER_PORT   443                    // HTTPS

// ── APN ตาม network operator ──
#define APN               "www.dtac.co.th" // DTAC (ตามซิม) | สำรอง: www.dtac.net | AIS="internet" | TRUE="internet.true.th"
#define GPRS_USER         ""
#define GPRS_PASS         ""

#define OTA_CHECK_SEC     300   // เช็ค OTA ทุก 5 นาที
#define TELEMETRY_SEC     10    // ส่งค่าขึ้น server (Monitor) ทุก 10 วิ — GPS/แบต/ฯลฯ
// ╚══════════════════════════════════════════════════════════════════╝

// ── PIN CONFIG (LILYGO T-A7670, ESP32-WROVER-E) ──────────────────
#define MODEM_RX_PIN      27   // ESP RX ← modem TX
#define MODEM_TX_PIN      26   // ESP TX → modem RX
#define MODEM_PWRKEY_PIN   4
#define MODEM_POWER_PIN   12   // BOARD_POWERON: HIGH = จ่ายไฟเลี้ยง modem
#define MODEM_RESET_PIN    5   // RESET modem (active HIGH ตาม T-A7670)
#define MODEM_BAUD        115200
#define LED_PIN           32   // LED สถานะ (cosmetic; เลี่ยง GPIO12 = POWERON)
#define BAT_ADC_PIN       35   // อ่านแรงดันแบต 18650 (board มี voltage divider 2:1)
#define BAT_LOW_VOLT      3.40 // ต่ำกว่านี้ = แบตใกล้หมด (เตือน/ลดงาน)

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
// ── AT debug dumper: echo modem TX/RX ไป Serial (เฉพาะโหมดเทส) ──
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
TinyGsm        modem(modemSerial);        // production: ต่อตรง ไม่ echo (เสถียรกว่า)
#endif
TinyGsmClientSecure gsmClient(modem, 0);  // MuxChannel 0 — API calls
// Channel 1 จะสร้างตอน download firmware เพื่อแยก connection

static char    g_deviceId[20]     = {0};
static char    g_deviceSecret[80] = {0};
static bool    g_provisioned      = false;
static uint32_t g_lastCheckMs     = 0;
static uint32_t g_lastTeleMs      = 0;       // ส่ง telemetry (GPS/ค่า) ครั้งล่าสุด
static uint32_t g_appliedSeq      = 0;       // B: rollout seq ที่ลงล่าสุด (เก็บ NVS) — กัน flap ตอน deploy/rollback
static volatile float g_vbat      = 0.0f;    // แรงดันแบตล่าสุด (loop อัปเดต, otaTask ส่งขึ้น server)
static bool    g_ledState         = false;

// ── CHIP ID ──────────────────────────────────────────────────────
// ── OTA info struct (วางก่อน function แรก กัน Arduino auto-prototype error) ──
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
//   A7670G power sequence ตาม SIMCom datasheet
// ─────────────────────────────────────────────────────────────────
static void modemPowerOn() {
  // 1) POWERON (GPIO12) จ่ายไฟเลี้ยง modem
  pinMode(MODEM_POWER_PIN, OUTPUT);
  digitalWrite(MODEM_POWER_PIN, HIGH);
  delay(100);

  // เปิด UART + เช็คว่า modem เปิดค้างอยู่ไหม (กรณี ESP reboot แต่ modem ยังเปิด)
  // ถ้าเปิดอยู่แล้ว → ข้าม PWRKEY (ไม่งั้น pulse จะ toggle ปิด modem → AT ไม่ตอบ)
  modemSerial.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
  delay(200);
  if (modem.testAT(1500)) { LOGDLN("[GSM] modem already on — skip power-on"); return; }

  // 2) RESET modem (T-A7670: active HIGH) — ตามโค้ดที่ใช้งานได้จริง
  pinMode(MODEM_RESET_PIN, OUTPUT);
  digitalWrite(MODEM_RESET_PIN, LOW);  delay(100);
  digitalWrite(MODEM_RESET_PIN, HIGH); delay(1000);
  digitalWrite(MODEM_RESET_PIN, LOW);

  // 3) PWRKEY pulse — A7670 ต้อง HIGH นาน 1000ms (เดิม 100ms สั้นไป modem ไม่ตื่น!)
  pinMode(MODEM_PWRKEY_PIN, OUTPUT);
  digitalWrite(MODEM_PWRKEY_PIN, LOW);  delay(100);
  digitalWrite(MODEM_PWRKEY_PIN, HIGH); delay(1000);
  digitalWrite(MODEM_PWRKEY_PIN, LOW);

  // ไม่ poll STATUS — ความพร้อมจริงเช็คด้วย testAT() ใน modemInit()
  LOGDLN("[GSM] power-on (POWERON+RESET+PWRKEY) done, modem booting...");
  delay(5000);   // A7670 boot ช้า รอ ~5s ก่อน AT
}

static void modemPowerOff() {
  // Toggle PWRKEY เพื่อปิด
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
  // ปิด CCH SSL service ให้สุด เพื่อให้ connect ครั้งถัดไปตั้ง CCHSET/CCHSTART ใหม่ได้
  // (ไม่งั้น request ที่ 2 จะ ERROR ที่ CCHSET เพราะ service ยังรันค้าง)
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
    "\"device_name\":\"" + g_deviceId + "\"," +
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
    LOG("[APIOTA] registered — รอ Approve ใน Dashboard\n");
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
  // B: ส่ง applied_seq → server ตัดสินจาก rollout counter (deploy/rollback flap-free)
  String path = "/api/device/check-update?version=" + urlEncode(CURRENT_VERSION) + "&seq=" + String(g_appliedSeq);
  String resp;
  int code = gsmHttpRequest("GET", path, "", resp, gsmClient);
  if (code == 404) {
    // device ถูกลบบน server → ล้าง NVS แล้ว provision ใหม่ (self-heal)
    LOGDLN("[APIOTA] device not found (404) — clearing NVS + re-provision");
    clearCredentials();
    if (doProvision()) LOG("[APIOTA] re-provisioned — รอ Approve ใน Dashboard\n");
    return inf;
  }
  if (code != 200) { LOGD("[APIOTA] checkUpdate HTTP %d\n", code); return inf; }
  inf.available  = (jget(resp, "update") == "true");
  inf.rolloutSeq = (uint32_t)jget(resp, "rollout_seq").toInt();   // B: seq ของ rollout นี้
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

  // Use MuxChannel 1 for download (แยกจาก API client)
  TinyGsmClientSecure dlClient(modem, 1);
  if (!dlClient.connect(host.c_str(), port)) {
    Serial.println("[OTA] download connect failed"); return false;
  }

  String req = "GET " + dlPath + " HTTP/1.1\r\n";
  req += "Host: " + host + "\r\n";
  // B2: device auth headers ตอนโหลด .bin (server เพิ่ม deviceAuth ที่ download endpoint เฟส 2)
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

  uint8_t buf[1024];   // A7670G LTE → ใช้ buffer ใหญ่ได้ (เร็วกว่า SIM800L มาก)
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
    // LED blink ระหว่าง download
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
  // B: บันทึก rollout seq ที่เพิ่งลงสำเร็จ → รอบหน้าไม่ดึง rollout เดิมซ้ำ (flap-free)
  { Preferences p; p.begin("apiota", false); p.putULong("seq", inf.rolloutSeq); p.end(); }
  Serial.println("[OTA] ✓ SUCCESS — rebooting in 3s...");
  delay(3000);
  ESP.restart();
  return true;
}

// ── LTE CONNECT ──────────────────────────────────────────────────
static bool lteConnect() {
  LOGD("[LTE] connecting (APN: %s)...\n", APN);
  // A7672X driver จบ gprsConnect ด้วย +CDNSCFG ที่ A7670 (LTE) ไม่รองรับ → return false
  // ทั้งที่ context ต่อแล้ว จึงเช็คจาก IP/CGATT จริงแทน return value
  modem.gprsConnect(APN, GPRS_USER, GPRS_PASS);   // ตั้ง PDP context (CGATT/CGPADDR)
  // รอ network พร้อม — retry กัน CREG ตอบเพี้ยนชั่วคราว (ไม่ fail/restart ทันที)
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

  // รอ SIM พร้อม (ไม่เกิน 30s)
  LOGD("[GSM] waiting SIM");
  for (int i = 0; i < 15; i++) {
    SimStatus s = modem.getSimStatus();
    if (s == SIM_READY) { LOGDLN(" ready"); break; }
    LOGD(".");
    delay(2000);
    if (i == 14) { LOGDLN(" SIM not ready!"); return false; }
  }

  // รอ network registration (ไม่เกิน 60s)
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

  // SSL: ตั้ง authmode=0 = ไม่ verify server cert (A7670 ไม่มี CA + นาฬิกาไม่ sync → verify ไม่ผ่าน)
  // ถ้าไม่ตั้ง modem จะ verify cert ของ apiota.net แล้ว fail -> CCHOPEN/connect failed
  modem.sendAT(GF("+CSSLCFG=\"authmode\",0,0"));
  modem.waitResponse();

  return true;
}

// ================================================================
//  GNSS + TELEMETRY — device ส่งค่าขึ้น server (ปุ่ม 📡 Monitor)
//    ส่ง GPS (ถ้าจับ fix ได้) + ค่าทั่วไปใน data (เช่น แบต/อุณหภูมิ/status)
// ================================================================
// เปิด GNSS (best-effort; ถ้าเปิดอยู่แล้วจะตอบ ERROR ก็ข้ามได้)
static void gnssOn() {
  modem.sendAT(GF("+CGNSSPWR=1"));
  modem.waitResponse(10000L);                  // OK
  modem.waitResponse(12000L, GF("READY!"));    // รอ GNSS engine พร้อม (URC) — best effort
  LOGD("[GNSS] powered on (รอจับดาวเทียม ครั้งแรกอาจ 30s-2นาที)\n");
}

// อ่านพิกัดจาก AT+CGNSSINFO → true ถ้ามี fix
//   รูปแบบ: +CGNSSINFO: <mode>,<GPSsv>,<GLONASSsv>,<BEIDOUsv>,<lat>,<N/S>,<lon>,<E/W>,<date>,<utc>,<alt>,<speed>,<course>,...
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
  if (n < 8 || tok[4].length() == 0 || tok[6].length() == 0) return false;   // ยังไม่มี fix
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

// ส่ง telemetry ขึ้น server: GPS (ถ้ามี) + data ทั่วไป
//   อยากเพิ่มค่าอื่น (temp/humid/status) → ใส่ใน dataBuf เป็น JSON
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
//  OTA TASK (FreeRTOS) — งาน modem/network/OTA แยกจาก loop หลัก
// ================================================================
static void otaTask(void* pv) {
  // init modem + LTE — ถ้า fail ให้ restart เพื่อ retry สะอาด (เสถียรกว่าค้าง)
  if (!modemInit())  { LOGLN("FATAL: modem init failed — restart"); ledBlink(10,100); vTaskDelay(pdMS_TO_TICKS(2000)); ESP.restart(); }
  if (!lteConnect()) { LOGLN("FATAL: LTE connect failed — restart"); ledBlink(5,200);  vTaskDelay(pdMS_TO_TICKS(2000)); ESP.restart(); }
  ledBlink(1, 300);
  gnssOn();   // เปิด GPS (จับ fix เบื้องหลัง — ค่าจะทยอยมาเมื่อเห็นดาวเทียม)

  // provision (ถ้ายังไม่มี credential)
  loadCredentials();
  if (!g_provisioned) {
    for (int i = 0; i < 3 && !g_provisioned; i++) {
      if (doProvision()) break;
      LOGD("[APIOTA] provision attempt %d/3 failed\n", i + 1);
      vTaskDelay(pdMS_TO_TICKS(5000));
    }
    if (!g_provisioned) { LOGLN("FATAL: provision failed — restart"); ledBlink(10,100); vTaskDelay(pdMS_TO_TICKS(2000)); ESP.restart(); }
  }

  // OTA check ทันทีหลัง boot
  { ApiotaOTA inf = checkUpdate(); if (inf.available) performOTA(inf); }
  digitalWrite(LED_PIN, HIGH);   // LED ติด = ทำงานปกติ
  LOG("=== APIOTA ready (device %s) ===\n", g_deviceId);

  // loop งาน OTA: reconnect ถ้าหลุด + เช็ค OTA ตาม interval
  for (;;) {
    if (!modem.isNetworkConnected()) {   // CREG — เชื่อถือได้ (ไม่อิง CCHADDR)
      LOGDLN("[LTE] disconnected — reconnecting...");
      digitalWrite(LED_PIN, LOW);
      lteConnect();
      digitalWrite(LED_PIN, HIGH);
    }
    uint32_t now = millis();
    // ส่ง telemetry (GPS/แบต) ขึ้น server ทุก TELEMETRY_SEC → ดูได้ที่ปุ่ม 📡 Monitor
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
    vTaskDelay(pdMS_TO_TICKS(1000));   // yield — ไม่บล็อก task อื่น
  }
}

// ================================================================
//  SETUP — สร้าง OTA task แล้วปล่อย loop() ให้งานลูกค้า
// ================================================================
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  uint32_t chipId = calcChipId();
  snprintf(g_deviceId, sizeof(g_deviceId), "ESP32_%06X", (unsigned)chipId);
  LOG("\n=== APIOTA T-A7670G  v%s  Device: %s ===\n", CURRENT_VERSION, g_deviceId);

  // modem/OTA ทำงานใน task แยก (core 0, stack 16KB สำหรับ OTA+verify)
  // loop() อิสระอยู่ core 1 — ใส่งานลูกค้าได้เลย ไม่ถูก OTA บล็อก
  xTaskCreatePinnedToCore(otaTask, "apiota_ota", 16384, NULL, 2, NULL, 0);
}

// ================================================================
//  BATTERY — อ่านแรงดันแบต 18650 (GPIO35 + voltage divider 2:1)
// ================================================================
static volatile bool g_batteryLow = false;   // otaTask เช็คได้ (เผื่อหยุด OTA ตอนแบตต่ำ)

static float readBatteryVoltage() {
  uint16_t s[15];
  for (int i = 0; i < 15; i++) { s[i] = analogReadMilliVolts(BAT_ADC_PIN); delayMicroseconds(300); }
  for (int i = 0; i < 15; i++)                  // sort หา median (กัน noise)
    for (int j = i + 1; j < 15; j++)
      if (s[j] < s[i]) { uint16_t t = s[i]; s[i] = s[j]; s[j] = t; }
  return (s[7] * 2.0f) / 1000.0f;               // median × 2 (divider) / 1000
}

static int batteryPercent(float v) {          // Li-ion โดยประมาณ
  if (v >= 4.20f) return 100;
  if (v <= 3.30f) return 0;
  return (int)((v - 3.30f) / (4.20f - 3.30f) * 100.0f);
}

// ================================================================
//  LOOP — งานลูกค้า (อิสระ ไม่ถูก network/OTA บล็อก)
// ================================================================
void loop() {
  static uint32_t lastBat = 0;
  if (millis() - lastBat >= 10000) {          // อ่านแบตทุก 10 วินาที
    lastBat = millis();
    float vbat = readBatteryVoltage();
    g_vbat = vbat;                              // เก็บให้ otaTask ส่งขึ้น server (Monitor)
    if (vbat < 2.5f) {                          // < 2.5V = ไม่มีแบตบนราง (สวิตช์ OFF / จ่ายผ่าน USB)
      g_batteryLow = false;
      LOG("[BAT] USB power (ไม่มีแบต / สวิตช์ OFF)\n");
    } else {
      int pct = batteryPercent(vbat);
      g_batteryLow = (vbat < BAT_LOW_VOLT);
      LOG("[BAT] %.2f V  (~%d%%)%s\n", vbat, pct, g_batteryLow ? "  ⚠ LOW" : "");
    }
  }

  // ───────────── ใส่งานอื่นของคุณตรงนี้ ─────────────
  //   อ่าน sensor / ควบคุม relay ฯลฯ — OTA+4G จัดการเองใน otaTask
  // ───────────────────────────────────────────────────
  vTaskDelay(pdMS_TO_TICKS(1000));
}
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           