/*
  ================================================================
   BasicAPIOTA — minimal APIOTA example
   WiFi + auto-provision + OTA + commands, all inside the library
   (logs every step over Serial)
  ================================================================
*/
#include <APIOTA.h>

// ── USER CONFIG ──────────────────────────────────────────────────
#define WIFI_SSID          "YOUR_WIFI_SSID"
#define WIFI_PASSWORD      "YOUR_WIFI_PASSWORD"
#define API_KEY            "ak_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // from apiota.net Dashboard
#define FIRMWARE_VERSION   "1.0.0"          // bump on every new build
#define DEVICE_NAME        "BasicESP32"     // shown in Dashboard (syncs on reflash)
#define OTA_CHECK_SEC      15               // test 15s; production recommended 300
#define STATUS_LED         2                // onboard LED (most ESP32 dev boards = GPIO2)

APIOTAClient APIOTA;

void setup() {
  Serial.begin(115200);
  pinMode(STATUS_LED, OUTPUT);

  APIOTA.setCheckInterval(OTA_CHECK_SEC);
  APIOTA.connectWiFi(WIFI_SSID, WIFI_PASSWORD);   // WiFi (helper in library)

  // commands from Dashboard / Telegram (reboot & check_update handled by the library)
  APIOTA.onCommand([](const String& cmd, const String& payload, uint32_t id){
    if (cmd == "led_on")  digitalWrite(STATUS_LED, HIGH);
    if (cmd == "led_off") digitalWrite(STATUS_LED, LOW);
  });

  APIOTA.begin(API_KEY, FIRMWARE_VERSION, DEVICE_NAME);   // provision + OTA + poll task
}

void loop() {
  APIOTA.tick();                                      // OTA check + keeps commands alive
  if (!APIOTA.isApproved()) { delay(500); return; }   // hold until ✓ Approve (or while Locked)

  // ── your application code here ──
  delay(1000);
}
