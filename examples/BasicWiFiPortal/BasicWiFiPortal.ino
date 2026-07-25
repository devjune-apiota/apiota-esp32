/*
  ================================================================
   BasicWiFiPortal — multitask OTA + WiFi captive portal
   ESP32 Dev Module (no display) | needs WiFiManager (tzapu)
   No hardcoded WiFi: first boot opens AP "APIOTA-ESP32-SETUP" —
   connect with a phone and pick your network.
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

volatile int g_ledMode = 0;   // 0 = blink, 1 = on, 2 = off

// WiFi portal + provisioning + OTA + command polling — runs on core 0
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
    vTaskDelay(pdMS_TO_TICKS(1000));   // yield to other tasks
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

// loop() = your application code — non-blocking, no delay()
void loop() {
  if (!APIOTA.isApproved()) {          // hold until ✓ Approve / Unlock (OTA + poll keep running)
    digitalWrite(STATUS_LED, (millis() / 150) % 2);   // fast blink = waiting for ✓ Approve / Unlock
    vTaskDelay(pdMS_TO_TICKS(100));
    return;
  }

  static uint32_t lastBlink = 0;
  static bool on = false;

  if (g_ledMode == 1) {
    digitalWrite(STATUS_LED, HIGH);
  } else if (g_ledMode == 2) {
    digitalWrite(STATUS_LED, LOW);
  } else if (millis() - lastBlink >= 500) {   // blink every 500 ms
    lastBlink = millis();
    on = !on;
    digitalWrite(STATUS_LED, on);
  }

  vTaskDelay(pdMS_TO_TICKS(10));   // cooperative yield
}
