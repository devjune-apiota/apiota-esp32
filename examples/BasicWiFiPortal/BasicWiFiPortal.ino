/*
  ================================================================
   BasicWiFiPortal — APIOTA Library Example
   ESP32 Dev Module (no display)
   multitasking OTA (no delay()) + WiFiManager captive portal
   ────────────────────────────────────────────────
   Like BasicMultiTask, but WiFi is configured through a captive portal
   instead of hardcoded credentials. On first boot (or when no known
   WiFi is found) the device opens an access point "APIOTA-ESP32-SETUP"
   — connect to it with a phone and pick your network.
   Dependency: WiFiManager (tzapu) — install via Library Manager
   add onCommand to receive led_on / led_off from the Dashboard
  ================================================================
*/
#include <WiFiManager.h>   // https://github.com/tzapu/WiFiManager
#include <APIOTA.h>

// ── USER CONFIG ──────────────────────────────────────────────────
#define API_KEY            "ak_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // from apiota.net Dashboard
#define FIRMWARE_VERSION   "1.0.0"          // bump on every new build
#define DEVICE_NAME        "BasicWiFiPortal"
#define OTA_CHECK_SEC      15               // test 15s; production recommended 300
#define STATUS_LED         2                // onboard LED (most ESP32 dev boards = GPIO2)
#define PORTAL_NAME        "APIOTA-ESP32-SETUP"   // WiFi config portal SSID
#define PORTAL_TIMEOUT_SEC 180             // close the portal after this if unused

APIOTAClient APIOTA;

// LED mode set by Dashboard commands, read by loop() (no delay needed)
volatile int g_ledMode = 0;   // 0 = blink, 1 = on, 2 = off

// ── OTA TASK ─────────────────────────────────────────────────────
// WiFiManager portal + provisioning + OTA + command polling, all on
// its own core, so loop() never has to block.
void apiotaTask(void* pv) {
  // WiFi via captive portal — no hardcoded SSID/password
  WiFiManager wm;
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT_SEC);
  if (!wm.autoConnect(PORTAL_NAME)) {
    Serial.println("[WiFi] portal timed out — restarting");
    ESP.restart();
  }

  APIOTA.setCheckInterval(OTA_CHECK_SEC);
  APIOTA.onCommand([](const String& cmd, const String& payload, uint32_t id){
    if (cmd == "led_on")  g_ledMode = 1;
    if (cmd == "led_off") g_ledMode = 2;
  });

  APIOTA.begin(API_KEY, FIRMWARE_VERSION, DEVICE_NAME);   // provision + OTA + poll

  for (;;) {
    APIOTA.tick();                     // check OTA on the configured interval
    vTaskDelay(pdMS_TO_TICKS(1000));   // yields the CPU to other tasks (NOT a busy delay)
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  // WiFiManager portal + OTA run on core 0; loop() stays free on core 1
  // (larger stack: the captive portal runs its own web server)
  xTaskCreatePinnedToCore(apiotaTask, "apiota_ota", 12288, NULL, 2, NULL, 0);
}

// ── loop ─────────────────────────────────────────────────────────
// Your application code — non-blocking, no delay()
void loop() {
  // ── Approval / Lock gate (v1.4.0) — OTA + command poll keep running in their task ──
  if (!APIOTA.isApproved()) {
    digitalWrite(STATUS_LED, (millis() / 150) % 2);   // fast blink = waiting for ✓ Approve / Unlock
    vTaskDelay(pdMS_TO_TICKS(100));
    return;                                           // application code does not run yet
  }

  static uint32_t lastBlink = 0;
  static bool on = false;

  if (g_ledMode == 1) {
    digitalWrite(STATUS_LED, HIGH);
  } else if (g_ledMode == 2) {
    digitalWrite(STATUS_LED, LOW);
  } else if (millis() - lastBlink >= 500) {   // blink every 500ms without delay()
    lastBlink = millis();
    on = !on;
    digitalWrite(STATUS_LED, on);
  }

  vTaskDelay(pdMS_TO_TICKS(10));   // small cooperative yield (not a blocking delay)
}
