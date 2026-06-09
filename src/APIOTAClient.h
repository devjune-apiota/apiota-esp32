// ═══════════════════════════════════════════════════════════════════
//   APIOTA Client — ESP32 OTA + Command + Provisioning Library
//   v1.2.0  ·  apiota.net
//   ───────────────────────────────────────────────────────────────
//   Usage (minimal):
//     #include <APIOTA.h>
//     APIOTAClient APIOTA;
//     void setup() {
//       Serial.begin(115200);
//       APIOTA.connectWiFi("ssid","pass");           // WiFi helper
//       APIOTA.begin("ak_xxx", "1.0.0", "My Device"); // provision + OTA + poll
//     }                                               // (library auto-logs ทาง Serial)
//     void loop() { APIOTA.tick(); }
//   Optional: APIOTA.onCommand([](String c,String p,uint32_t id){ if(c=="reboot") ESP.restart(); });
// ═══════════════════════════════════════════════════════════════════
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Preferences.h>
#include <functional>
#include "mbedtls/sha256.h"
#include "mbedtls/pk.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_ota_ops.h"
#include <time.h>

#ifndef APIOTA_DEFAULT_SERVER
  #define APIOTA_DEFAULT_SERVER "https://apiota.net"
#endif

#ifndef APIOTA_DEFAULT_PUBKEY
static const char* APIOTA_DEFAULT_PUBKEY = R"KEY(
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
#endif

#ifndef APIOTA_DEFAULT_CACERT
// ISRG Root X1 — Let's Encrypt root CA used by apiota.net.
// Verified SHA256 fingerprint 96:BC:EC:06:26:49:76:F3:74:60:77:9A:CF:28:C5:A7:
//                             CF:E8:A3:C0:AA:E1:1A:8F:FC:EE:05:C0:BD:DF:08:C6
// Valid until 2035-06-04. Replaces setInsecure() (B-06).
static const char* APIOTA_DEFAULT_CACERT = R"CERT(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)CERT";
#endif

// ── PUBLIC TYPES ──────────────────────────────────────────────────
struct APIOTAUpdateInfo {
  bool     available  = false;
  String   version;
  String   url;
  String   sha256;
  String   signature;
  int      size       = 0;
  uint32_t rolloutSeq = 0;   // B: rollout sequence (deploy/rollback flap-free)
};

enum APIOTAState : uint8_t {
  APST_IDLE, APST_PROVISIONING, APST_CHECKING, APST_DOWNLOADING,
  APST_VERIFYING, APST_FLASHING, APST_SUCCESS, APST_FAILED, APST_ROLLBACK
};

// Reason a device is told to stop by the server (B-11)
enum APIOTAExpiry : uint8_t {
  APEX_NONE, APEX_LIFT_TIME, APEX_WORKING, APEX_PLAN_LIMIT, APEX_BLOCKED
};

using APIOTACommandCb     = std::function<void(const String& cmd, const String& payloadJson, uint32_t cmdId)>;
using APIOTAProgressCb    = std::function<void(APIOTAState state, int pct, const char* msg)>;
using APIOTAProvisionedCb = std::function<void(const String& deviceId, const String& deviceSecret)>;
using APIOTAErrorCb       = std::function<void(const String& errorMsg)>;
using APIOTAExpiredCb     = std::function<void(APIOTAExpiry reason, const String& message)>;

// ═══════════════════════════════════════════════════════════════════
//   APIOTAClient
// ═══════════════════════════════════════════════════════════════════
class APIOTAClient {
public:
  bool begin(const char* apiKey, const char* firmwareVersion, const char* deviceName = nullptr) {
    _apiKey = apiKey ? apiKey : "";
    _fwVer  = firmwareVersion ? firmwareVersion : "0.0.0";
    if (deviceName && *deviceName) _deviceName = deviceName;
    if (_server.length() == 0) _server = APIOTA_DEFAULT_SERVER;
    if (_pubKey.length() == 0) _pubKey = APIOTA_DEFAULT_PUBKEY;
    if (_caCert.length() == 0 && !_insecure) _caCert = APIOTA_DEFAULT_CACERT;

    if (_apiKey.length() < 6) { _emitError("api_key invalid"); return false; }

    if (_deviceId.length() == 0) _deriveDeviceId();
    _loadFromNVS();
    _syncTime();   // needed before TLS cert validation (B-06)

    if (_deviceSecret.length() == 0) {
      if (!autoProvision()) return false;
    } else {
      _emitProgress(APST_PROVISIONING, 50, "verifying cached credentials...");
      if (!_validateRegistration()) {
        _emitProgress(APST_PROVISIONING, 70, "stale secret, re-provisioning...");
        _clearNVS();
        _deviceSecret = "";
        if (!autoProvision()) return false;
      } else {
        _emitProgress(APST_IDLE, 100, "credentials valid (cached)");
      }
    }

    _provisioned = true;
    _pollEnabled = true;

    if (!_pollTaskHandle) {
      xTaskCreatePinnedToCore(&APIOTAClient::_pollTaskTramp, "apiota_poll", 6144, this, 1, &_pollTaskHandle, 0);
    }
    return true;
  }

  // Connect WiFi (helper) — ให้ sketch สั้นลง ไม่ต้องเขียน loop เอง
  bool connectWiFi(const char* ssid, const char* pass, uint32_t timeoutMs = 20000) {
    if (WiFi.status() == WL_CONNECTED) return true;
    Serial.printf("[APIOTA] WiFi connecting: %s\n", ssid ? ssid : "");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
      delay(250); Serial.print(".");
    }
    bool ok = (WiFi.status() == WL_CONNECTED);
    if (ok) Serial.printf("\n[APIOTA] WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());
    else    Serial.println("\n[APIOTA] WiFi connect timeout");
    return ok;
  }

  void setServer(const char* url)         { _server = url; }
  void setPublicKey(const char* pem)      { _pubKey = pem; }
  void setCACert(const char* pem)         { _caCert = pem; _insecure = false; }
  void setInsecure(bool b = true)         { _insecure = b; }  // opt-out of TLS verify
  void setDeviceId(const char* id)        { _deviceId = id; }
  void setDeviceName(const char* name)    { _deviceName = name; }
  void setCheckInterval(uint32_t seconds) { _checkIntervalMs = seconds * 1000; }
  void setPollInterval(uint32_t seconds)  { _pollIdleMs = seconds * 1000; }
  void enableAutoCheck(bool b)            { _autoCheck = b; }

  APIOTAClient& onCommand(APIOTACommandCb cb)         { _cmdCb  = cb; return *this; }
  APIOTAClient& onProgress(APIOTAProgressCb cb)       { _progCb = cb; return *this; }
  APIOTAClient& onProvisioned(APIOTAProvisionedCb cb) { _provCb = cb; return *this; }
  APIOTAClient& onError(APIOTAErrorCb cb)             { _errCb  = cb; return *this; }
  APIOTAClient& onExpired(APIOTAExpiredCb cb)         { _expCb  = cb; return *this; }

  bool          isProvisioned() const   { return _provisioned; }
  const String& getDeviceId() const     { return _deviceId; }
  const String& getDeviceSecret() const { return _deviceSecret; }
  APIOTAState   getState() const        { return _state; }

  // ── Telemetry — ส่งค่าอะไรก็ได้ขึ้น server (โชว์ใน Dashboard → 🖥 Console) ──
  //   dataJson = JSON object string เช่น "{\"heap_kb\":120,\"rssi\":-60}"
  //   มี GPS ก็ใช้ overload แบบใส่ lat/lon ได้
  //   คืน true ถ้า server รับ (HTTP 200) · ต้อง provisioned ก่อน
  bool sendTelemetry(const String& dataJson) {
    return _sendTelemetry(dataJson, false, 0, 0);
  }
  bool sendTelemetry(double lat, double lon, const String& dataJson) {
    return _sendTelemetry(dataJson, true, lat, lon);
  }

  void tick() {
    if (!_provisioned) return;
    if (_autoCheck && _checkIntervalMs > 0) {
      uint32_t now = millis();
      if (now - _lastCheckMs >= _checkIntervalMs) {
        _lastCheckMs = now;
        APIOTAUpdateInfo inf = checkUpdate();
        if (inf.available) updateFirmware(inf);
      }
    }
  }

  bool timeTick(uint32_t seconds) {
    uint32_t now = millis();
    if (now - _lastTickMs >= seconds * 1000UL) { _lastTickMs = now; return true; }
    return false;
  }

  bool autoProvision() {
    _emitProgress(APST_PROVISIONING, 0, "registering device...");

    String mac      = WiFi.macAddress();
    String chipMdl  = String(ESP.getChipModel());
    uint32_t flashK = ESP.getFlashChipSize() / 1024;
    uint8_t cores   = ESP.getChipCores();
    uint8_t rev     = ESP.getChipRevision();
    String sdk      = String(ESP.getSdkVersion());
    uint32_t chipId = _calcChipId();
    char chipIdHex[16];
    snprintf(chipIdHex, sizeof(chipIdHex), "0x%06X", (unsigned)chipId);

    String chipInfo = String("{") +
      "\"mac\":\"" + mac + "\"," +
      "\"chip\":\"" + chipMdl + "\"," +
      "\"chip_id\":" + chipId + "," +
      "\"chip_id_hex\":\"" + chipIdHex + "\"," +
      "\"revision\":" + rev + "," +
      "\"cores\":" + cores + "," +
      "\"flash_kb\":" + flashK + "," +
      "\"sdk\":\"" + sdk + "\"" +
    "}";

    String body = String("{") +
      "\"api_key\":\"" + _escape(_apiKey) + "\"," +
      "\"device_id\":\"" + _escape(_deviceId) + "\"," +
      "\"device_name\":\"" + _escape(_deviceName) + "\"," +
      "\"version\":\"" + _escape(_fwVer) + "\"," +
      "\"chip_info\":" + chipInfo +
    "}";

    String resp;
    int code = _httpPost("/api/device/provision", body, resp);
    if (code != 200 && code != 201) {
      String err = _jget(resp, "error");
      if (code == 402) {
        String maxDev  = _jget(resp, "max_devices");
        String curPlan = _jget(resp, "current_plan");
        String msgTh   = _jget(resp, "message_th");
        _emitProgress(APST_FAILED, -1, "PLAN LIMIT EXCEEDED");
        Serial.println("================================================");
        Serial.println("X DEVICE LIMIT EXCEEDED");
        Serial.printf("  Plan: %s\n", curPlan.c_str());
        Serial.printf("  Max:  %s\n", maxDev.c_str());
        if (msgTh.length()) Serial.println("  " + msgTh);
        Serial.println("  Upgrade: https://apiota.net/apiota-pricing.html");
        Serial.println("================================================");
        _emitError("plan_limit_exceeded");
        _planLimitBlocked = true;
        return false;
      }
      _emitError("Provision failed: HTTP " + String(code) + (err.length() ? (" - " + err) : ""));
      return false;
    }

    _deviceId     = _jget(resp, "device_id");
    _deviceSecret = _jget(resp, "device_secret");
    if (_deviceId.length() == 0 || _deviceSecret.length() == 0) {
      _emitError("Provision response invalid");
      return false;
    }
    _saveToNVS();
    _provisioned = true;
    if (_provCb) _provCb(_deviceId, _deviceSecret);
    _emitProgress(APST_IDLE, 100, "device provisioned");
    return true;
  }

  APIOTAUpdateInfo checkUpdate() {
    APIOTAUpdateInfo inf;
    if (!_provisioned) return inf;
    _emitProgress(APST_CHECKING, 0, "checking update...");
    // B: ส่ง applied_seq → server ตัดสินจาก rollout counter (deploy/rollback flap-free)
    String path = "/api/device/check-update?version=" + _urlEncode(_fwVer) + "&seq=" + String(_appliedSeq);
    String resp;
    int code = _httpGet(path, resp);
    if (code == 402) { _emitExpired(APEX_LIFT_TIME, _jget(resp, "error")); return inf; }
    if (code == 403 && _jget(resp, "error") == "working_expired") {
      _emitExpired(APEX_WORKING, "working_expired"); return inf;
    }
    if (code == 403 && resp.indexOf("device_locked") >= 0) {
      if (!_blockedNotified) { _blockedNotified = true; if (_expCb) _expCb(APEX_BLOCKED, _jget(resp, "error")); }
      return inf;
    }
    if (code != 200) { _emitError("checkUpdate HTTP " + String(code)); return inf; }
    inf.available  = (_jget(resp, "update") == "true");
    inf.rolloutSeq = (uint32_t)_jget(resp, "rollout_seq").toInt();   // B: seq ของ rollout นี้
    if (inf.available) {
      inf.version   = _jget(resp, "version");
      inf.url       = _jget(resp, "url");
      inf.sha256    = _jget(resp, "sha256");
      inf.sha256.toLowerCase();  // normalize lowercase — server อาจส่ง upper/lower
      inf.signature = _jget(resp, "signature");
      _emitProgress(APST_CHECKING, 100, ("new version: " + inf.version).c_str());
    } else {
      _emitProgress(APST_IDLE, 100, "up to date");
    }
    return inf;
  }

  bool updateFirmware() {
    APIOTAUpdateInfo inf = checkUpdate();
    if (!inf.available) return false;
    return updateFirmware(inf);
  }

  bool updateFirmware(const APIOTAUpdateInfo& inf) {
    if (!_provisioned || !inf.available) return false;
    _reportLog("started", _fwVer.c_str(), inf.version.c_str(), "OTA initiated");
    if (!_verifySignature(inf.sha256, inf.signature)) {
      _reportLog("failed", _fwVer.c_str(), inf.version.c_str(), "signature verify failed");
      _emitProgress(APST_FAILED, -1, "sig invalid");
      return false;
    }
    _reportLog("verifying", _fwVer.c_str(), inf.version.c_str(), "signature OK");
    if (!_performOTA(inf)) {
      _reportLog("failed", _fwVer.c_str(), inf.version.c_str(), "OTA download/flash failed");
      return false;
    }
    _reportLog("success", _fwVer.c_str(), inf.version.c_str(), "OTA complete, rebooting");
    // B: บันทึก rollout seq ที่เพิ่งลงสำเร็จ → รอบหน้าไม่ดึง rollout เดิมซ้ำ (flap-free)
    _appliedSeq = inf.rolloutSeq;
    _prefs.begin("apiota", false);
    _prefs.putULong("seq", _appliedSeq);
    _prefs.end();
    _emitProgress(APST_SUCCESS, 100, "success, rebooting...");
    delay(1500);
    ESP.restart();
    return true;
  }

  void forceCheck() { _lastCheckMs = 0; }
  void log(const String& msg) { _reportLog("started", _fwVer.c_str(), "", msg.c_str()); }

  bool ackCommand(uint32_t cmdId, const char* status, const char* result = "") {
    String body = String("{\"command_id\":") + cmdId +
                  ",\"status\":\"" + status + "\"," +
                  "\"result\":\"" + _escape(result) + "\"}";
    String resp;
    int code = _httpPost("/api/device/command-ack", body, resp);
    return (code == 200);
  }

private:
  String _apiKey, _fwVer;
  String _deviceId, _deviceSecret, _deviceName;
  String _server, _pubKey, _caCert;
  bool _insecure   = false;
  bool _timeSynced = false;
  bool _provisioned      = false;
  bool _autoCheck        = true;
  bool _pollEnabled      = false;
  bool _planLimitBlocked = false;
  bool _blockedNotified  = false;   // locked-by-owner notified (recover when unlocked)
  uint32_t _checkIntervalMs = 5UL * 60UL * 1000UL;
  uint32_t _pollIdleMs      = 1000;
  uint32_t _lastCheckMs = 0;
  uint32_t _lastTickMs  = 0;
  uint32_t _cachedChipId = 0;
  APIOTAState _state = APST_IDLE;
  TaskHandle_t _pollTaskHandle = nullptr;
  Preferences _prefs;

  APIOTACommandCb     _cmdCb;
  APIOTAProgressCb    _progCb;
  APIOTAProvisionedCb _provCb;
  APIOTAErrorCb       _errCb;
  APIOTAExpiredCb     _expCb;

  uint32_t _calcChipId() {
    uint32_t chipId = 0;
    for (int i = 0; i < 17; i = i + 8)
      chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
    return chipId;
  }

  void _deriveDeviceId() {
    uint32_t chipId = _calcChipId();
    char buf[20];
    snprintf(buf, sizeof(buf), "ESP32_%06X", (unsigned)chipId);
    _deviceId = buf;
    if (_deviceName.length() == 0) _deviceName = _deviceId;
  }

  uint32_t _appliedSeq = 0;   // B: rollout seq ที่ลงล่าสุด (เก็บใน NVS)
  void _loadFromNVS() {
    _prefs.begin("apiota", false);
    String savedId  = _prefs.getString("dev_id", "");
    String savedSec = _prefs.getString("dev_sec", "");
    if (savedId == _deviceId && savedSec.length() > 0) _deviceSecret = savedSec;
    _appliedSeq = _prefs.getULong("seq", 0);
    _prefs.end();
  }
  void _saveToNVS() {
    _prefs.begin("apiota", false);
    _prefs.putString("dev_id", _deviceId);
    _prefs.putString("dev_sec", _deviceSecret);
    _prefs.end();
  }
  void _clearNVS() {
    _prefs.begin("apiota", false);
    _prefs.remove("dev_id");
    _prefs.remove("dev_sec");
    _prefs.end();
  }

  bool _validateRegistration() {
    if (WiFi.status() != WL_CONNECTED) return true;
    String path = "/api/device/check-update?version=" + _urlEncode(_fwVer);
    String resp;
    int code = _httpGet(path, resp);
    if (code == 200) return true;
    if (code == 402) return true;  // lift-time expired: registration valid -> surfaced via onExpired
    if (code == 403 && _jget(resp, "error") == "working_expired") return true;  // working expired but valid
    if (code == 404 || code == 403) return false;  // genuinely rejected -> re-provision
    return true;
  }

  // ── TLS helpers (B-06) ──────────────────────────────────────────
  void _syncTime() {
    if (_timeSynced || _insecure) return;
    if (WiFi.status() != WL_CONNECTED) return;
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");
    time_t now = time(nullptr);
    int tries = 0;
    while (now < 1700000000 && tries < 40) { delay(250); now = time(nullptr); tries++; }
    if (now >= 1700000000) {
      _timeSynced = true;
      Serial.printf("[APIOTA] time synced (%ld)\n", (long)now);
    } else {
      Serial.println("[APIOTA] WARN: NTP sync failed - TLS verify may fail");
    }
  }
  void _secureBegin(WiFiClientSecure& client) {
    if (_insecure) { client.setInsecure(); return; }
    if (!_timeSynced) _syncTime();
    if (_caCert.length()) client.setCACert(_caCert.c_str());
    else                  client.setInsecure();
  }
  int _httpGet(const String& path, String& outBody) {
    WiFiClientSecure client; _secureBegin(client);
    HTTPClient http;
    if (!http.begin(client, _server + path)) return -1;
    _addAuthHeaders(http);
    http.setTimeout(15000);
    int code = http.GET();
    if (code > 0) outBody = http.getString();
    http.end();
    return code;
  }
  int _httpPost(const String& path, const String& body, String& outBody) {
    WiFiClientSecure client; _secureBegin(client);
    HTTPClient http;
    if (!http.begin(client, _server + path)) return -1;
    http.addHeader("Content-Type", "application/json; charset=utf-8");
    _addAuthHeaders(http);
    http.setTimeout(15000);
    int code = http.POST((uint8_t*)body.c_str(), body.length());
    if (code > 0) outBody = http.getString();
    http.end();
    return code;
  }
  int _httpLongPoll(const String& path, String& outBody, uint32_t timeoutMs) {
    WiFiClientSecure client; _secureBegin(client);
    HTTPClient http;
    if (!http.begin(client, _server + path)) return -1;
    _addAuthHeaders(http);
    http.setTimeout(timeoutMs);
    int code = http.GET();
    if (code > 0) outBody = http.getString();
    http.end();
    return code;
  }
  void _addAuthHeaders(HTTPClient& http) {
    if (_deviceId.length())     http.addHeader("X-Device-ID", _deviceId);
    if (_deviceSecret.length()) http.addHeader("X-Device-Secret", _deviceSecret);
    if (_cachedChipId == 0) _cachedChipId = _calcChipId();
    http.addHeader("X-Chip-ID", String(_cachedChipId));
  }

  bool _sendTelemetry(const String& dataJson, bool hasGps, double lat, double lon) {
    if (!_provisioned || _deviceSecret.length() == 0) return false;
    String body = "{";
    if (hasGps) { body += "\"lat\":" + String(lat, 6) + ",\"lon\":" + String(lon, 6) + ","; }
    body += "\"data\":";
    body += (dataJson.length() ? dataJson : "{}");
    body += "}";
    String resp;
    int code = _httpPost("/api/device/telemetry", body, resp);
    return code == 200;
  }

  static String _jget(const String& json, const char* key) {
    String needle = String("\"") + key + "\":";
    int i = json.indexOf(needle);
    if (i < 0) return "";
    i += needle.length();
    while (i < (int)json.length() && (json[i] == ' ' || json[i] == '\t')) i++;
    if (i >= (int)json.length()) return "";
    char c = json[i];
    if (c == '"') {
      int e = i + 1;
      while (e < (int)json.length() && json[e] != '"') {
        if (json[e] == '\\' && e + 1 < (int)json.length()) e++;
        e++;
      }
      return json.substring(i + 1, e);
    }
    int e = i;
    while (e < (int)json.length() && json[e] != ',' && json[e] != '}' && json[e] != '\n' && json[e] != '\r') e++;
    String v = json.substring(i, e);
    v.trim();
    return v;
  }
  static String _escape(const String& s) {
    String r = s; r.replace("\\", "\\\\"); r.replace("\"", "\\\""); return r;
  }
  static String _urlEncode(const String& s) {
    String r = "";
    for (size_t i = 0; i < s.length(); i++) {
      char c = s[i];
      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
          c == '-' || c == '_' || c == '.' || c == '~') r += c;
      else { char b[4]; snprintf(b, sizeof(b), "%%%02X", (uint8_t)c); r += b; }
    }
    return r;
  }

  void _emitProgress(APIOTAState st, int pct, const char* msg) {
    _state = st;
    if (_progCb) _progCb(st, pct, msg ? msg : "");
    Serial.printf("[APIOTA] %s %d%% %s\n", _stateName(st), pct, msg ? msg : "");
  }
  void _emitError(const String& msg) {
    _state = APST_FAILED;
    if (_errCb) _errCb(msg);
    Serial.println("[APIOTA ERR] " + msg);
  }
  void _emitExpired(APIOTAExpiry reason, const String& message) {
    _planLimitBlocked = true;   // stop hammering the server
    if (_expCb) _expCb(reason, message);
    Serial.printf("[APIOTA] device stopped by server (reason %d): %s\n",
                  (int)reason, message.c_str());
  }
  static const char* _stateName(APIOTAState s) {
    switch (s) {
      case APST_IDLE:         return "idle";
      case APST_PROVISIONING: return "provision";
      case APST_CHECKING:     return "check";
      case APST_DOWNLOADING:  return "download";
      case APST_VERIFYING:    return "verify";
      case APST_FLASHING:     return "flash";
      case APST_SUCCESS:      return "success";
      case APST_FAILED:       return "failed";
      case APST_ROLLBACK:     return "rollback";
      default:                return "?";
    }
  }

  void _reportLog(const char* status, const char* fromVer, const char* toVer, const char* message) {
    String body = String("{\"status\":\"") + status +
                  "\",\"from_version\":\"" + (fromVer ? fromVer : "") +
                  "\",\"to_version\":\""   + (toVer   ? toVer   : "") +
                  "\",\"message\":\""      + _escape(message ? message : "") + "\"}";
    String resp;
    _httpPost("/api/device/ota-log", body, resp);
  }

  bool _verifySignature(const String& sha256Hex, const String& sigB64) {
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
    if (mbedtls_pk_parse_public_key(&pk, (const uint8_t*)_pubKey.c_str(), _pubKey.length() + 1) != 0) {
      mbedtls_pk_free(&pk); return false;
    }
    int r = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest, sizeof(digest), sig, slen);
    mbedtls_pk_free(&pk);
    return (r == 0);
  }

  bool _performOTA(const APIOTAUpdateInfo& inf) {
    _emitProgress(APST_DOWNLOADING, 0, "downloading...");
    _reportLog("downloading", _fwVer.c_str(), inf.version.c_str(), "starting");

    WiFiClientSecure client; _secureBegin(client);
    HTTPClient http;
    if (!http.begin(client, inf.url)) { _emitProgress(APST_FAILED, -1, "HTTPS begin fail"); return false; }
    _addAuthHeaders(http);   // B2: ส่ง X-Device-ID/Secret/Chip-ID ตอนโหลด .bin (เฟส 2: server เพิ่ม deviceAuth ที่ download endpoint)
    http.setTimeout(30000);
    int code = http.GET();
    if (code != HTTP_CODE_OK) { _emitProgress(APST_FAILED, -1, "HTTP error"); http.end(); return false; }

    int total = http.getSize();
    if (total <= 0) { _emitProgress(APST_FAILED, -1, "invalid size"); http.end(); return false; }
    if (!Update.begin(total)) { _emitProgress(APST_FAILED, -1, "no flash space"); http.end(); return false; }

    WiFiClient* stream = http.getStreamPtr();
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);

    uint8_t buf[2048];
    int written = 0;
    uint32_t lastUI = 0, lastData = millis();
    while (http.connected() && written < total) {
      size_t avail = stream->available();
      if (!avail) {
        if (millis() - lastData > 15000) {
          _emitProgress(APST_FAILED, -1, "OTA timeout");
          Update.abort(); http.end(); mbedtls_sha256_free(&sha); return false;
        }
        vTaskDelay(1);
        continue;
      }
      int rd = stream->readBytes(buf, min((int)avail, (int)sizeof(buf)));
      if (rd <= 0) { vTaskDelay(1); continue; }
      lastData = millis();
      mbedtls_sha256_update(&sha, buf, rd);
      if (Update.write(buf, rd) != (size_t)rd) {
        _emitProgress(APST_FAILED, -1, "flash write err");
        Update.abort(); http.end(); mbedtls_sha256_free(&sha); return false;
      }
      written += rd;
      uint32_t now = millis();
      if (now - lastUI >= 300) {
        lastUI = now;
        int pct = (int)((int64_t)written * 100 / total);
        _emitProgress(APST_DOWNLOADING, pct, "downloading");
      }
    }

    uint8_t hash[32];
    mbedtls_sha256_finish(&sha, hash);
    mbedtls_sha256_free(&sha);
    char calc[65];
    for (int i = 0; i < 32; i++) sprintf(calc + i * 2, "%02x", hash[i]);
    calc[64] = '\0';

    String sha256cmp = inf.sha256; sha256cmp.toLowerCase();
    if (sha256cmp.length() > 0 && sha256cmp != String(calc)) {
      char mismatch[80];
      snprintf(mismatch, sizeof(mismatch), "SHA256 mismatch exp:%.16s calc:%.16s", sha256cmp.c_str(), calc);
      _emitProgress(APST_ROLLBACK, -1, mismatch);
      _reportLog("rollback", _fwVer.c_str(), inf.version.c_str(), mismatch);
      Update.abort(); http.end(); return false;
    }
    _emitProgress(APST_FLASHING, 95, "flashing...");
    _reportLog("flashing", _fwVer.c_str(), inf.version.c_str(), "SHA256 OK");
    if (!Update.end(true) || !Update.isFinished()) {
      _emitProgress(APST_FAILED, -1, "Update.end fail");
      http.end(); return false;
    }
    http.end();
    return true;
  }

  static void _pollTaskTramp(void* arg) {
    static_cast<APIOTAClient*>(arg)->_pollTask();
  }
  void _pollTask() {
    vTaskDelay(pdMS_TO_TICKS(8000));
    int authFailCount = 0;
    for (;;) {
      if (!_pollEnabled || !_provisioned || WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        continue;
      }
      if (_planLimitBlocked) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        continue;
      }
      String resp;
      int code = _httpLongPoll("/api/device/poll", resp, 30000);
      if (code == 200) {
        authFailCount = 0;
        if (_blockedNotified) { _blockedNotified = false; _emitProgress(APST_IDLE, 0, "device unlocked"); }
        uint32_t cmdId = (uint32_t)_jget(resp, "command_id").toInt();
        String cmd = _jget(resp, "command");
        int pStart = resp.indexOf("\"payload\":");
        String payload = "{}";
        if (pStart >= 0) {
          int b = resp.indexOf("{", pStart);
          int e = resp.indexOf("}", b);
          if (b > 0 && e > b) payload = resp.substring(b, e + 1);
        }
        if (cmdId > 0 && cmd.length() > 0) _dispatchCommand(cmdId, cmd, payload);
        vTaskDelay(pdMS_TO_TICKS(_pollIdleMs));
      } else if (code == 204) {
        authFailCount = 0;
        if (_blockedNotified) { _blockedNotified = false; _emitProgress(APST_IDLE, 0, "device unlocked"); }
        vTaskDelay(pdMS_TO_TICKS(100));
      } else if (code == 404) {
        authFailCount++;
        if (authFailCount >= 2) {
          _clearNVS(); _deviceSecret = ""; _provisioned = false;
          if (autoProvision()) { _provisioned = true; authFailCount = 0; }
          else vTaskDelay(pdMS_TO_TICKS(30000));
        } else {
          vTaskDelay(pdMS_TO_TICKS(5000));
        }
      } else if (code == 403) {
        // 403 จาก owner-status (locked / pending approval) — อย่าล้าง NVS / re-provision
        if (resp.indexOf("device_locked") >= 0) {
          if (!_blockedNotified) {
            _blockedNotified = true;
            Serial.println("[APIOTA] device LOCKED by owner");
            if (_expCb) _expCb(APEX_BLOCKED, _jget(resp, "error"));
          }
          authFailCount = 0;
          vTaskDelay(pdMS_TO_TICKS(15000));   // คอย unlock แล้วกู้คืนเอง
        } else if (resp.indexOf("device_pending_approval") >= 0) {
          authFailCount = 0;
          vTaskDelay(pdMS_TO_TICKS(15000));
        } else if (resp.indexOf("working_expired") >= 0) {
          _emitExpired(APEX_WORKING, "working_expired");
        } else {
          // 403 จริง (invalid secret) → re-provision
          authFailCount++;
          if (authFailCount >= 3) {
            _clearNVS(); _deviceSecret = ""; _provisioned = false;
            if (autoProvision()) { _provisioned = true; authFailCount = 0; }
            else vTaskDelay(pdMS_TO_TICKS(60000));
          } else {
            vTaskDelay(pdMS_TO_TICKS(30000));
          }
        }
      } else {
        // code <= 0 = ต่อ TLS ไม่ติด (มัก heap ไม่พอ ตอน OTA-check/telemetry แย่ง) → ถอยสั้น กู้คืนเร็ว
        vTaskDelay(pdMS_TO_TICKS(2000));
      }
    }
  }

  void _dispatchCommand(uint32_t cmdId, const String& cmd, const String& payload) {
    if (cmd == "reboot") {
      ackCommand(cmdId, "acked", "rebooting"); delay(800); ESP.restart(); return;
    }
    if (cmd == "check_update") {
      ackCommand(cmdId, "acked", "checking"); _lastCheckMs = 0; return;
    }
    if (_cmdCb) {
      _cmdCb(cmd, payload, cmdId);
      ackCommand(cmdId, "acked", "");
    } else {
      ackCommand(cmdId, "failed", "no handler");
    }
  }
};
