/*
  ================================================================
   BasicOTA — APIOTA Library Example
   ESP32 Dev Module (no display) — minimal + LED command
   ────────────────────────────────────────────────
   WiFi + provision + OTA + RSA/SHA verify + TLS(CA) + command poll
   all handled inside the library + logs every step over Serial
   add onCommand to receive led_on / led_off from the Dashboard
  ================================================================
*/
#include <APIOTA.h>

// ── USER CONFIG ──────────────────────────────────────────────────
#define WIFI_SSID          "YOUR_WIFI_SSID"
#define WIFI_PASSWORD      "YOUR_WIFI_PASSWORD"
#define API_KEY            "ak_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // from apiota.net Dashboard
#define FIRMWARE_VERSION   "1.0.0"          // bump on every new build
#define DEVICE_NAME        "BasicESP32"
#define OTA_CHECK_SEC      15               // test 15s; production recommended 300
#define STATUS_LED         2                // onboard LED (most ESP32 dev boards = GPIO2)

APIOTAClient APIOTA;

void setup() {
  Serial.begin(115200);
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  APIOTA.setCheckInterval(OTA_CHECK_SEC);
  APIOTA.connectWiFi(WIFI_SSID, WIFI_PASSWORD);   // WiFi (helper in library)

  // receive commands from Dashboard / Telegram (reboot & check_update handled by the library)
  APIOTA.onCommand([](const String& cmd, const String& payload, uint32_t id){
    if (cmd == "led_on")  digitalWrite(STATUS_LED, HIGH);
    if (cmd == "led_off") digitalWrite(STATUS_LED, LOW);
  });

  APIOTA.begin(API_KEY, FIRMWARE_VERSION, DEVICE_NAME);   // provision + OTA + poll task
}

void loop() {
  APIOTA.tick();   // auto-check OTA on the configured interval
  delay(1000);
}
