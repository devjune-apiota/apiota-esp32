/*
  ================================================================
   BasicMultiTask — OTA + commands in a FreeRTOS task
   ESP32 Dev Module (no display)
   Like BasicAPIOTA, but loop() stays free for your own code.
  ================================================================
*/
#include <APIOTA.h>

// ── USER CONFIG ──────────────────────────────────────────────────
#define WIFI_SSID          "YOUR_WIFI_SSID"
#define WIFI_PASSWORD      "YOUR_WIFI_PASSWORD"
#define API_KEY            "ak_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // from apiota.net Dashboard
#define FIRMWARE_VERSION   "1.0.0"          // bump on every new build
#define DEVICE_NAME        "BasicMultiTask"
#define OTA_CHECK_SEC      15               // test 15s; production recommended 300
#define STATUS_LED         2                // onboard LED (most ESP32 dev boards = GPIO2)

APIOTAClient APIOTA;

volatile int g_ledMode = 0;   // 0 = blink, 1 = on, 2 = off

// WiFi + provisioning + OTA + command polling — runs on core 0
void apiotaTask(void* pv) {
  APIOTA.setCheckInterval(OTA_CHECK_SEC);
  APIOTA.connectWiFi(WIFI_SSID, WIFI_PASSWORD);   // WiFi (helper in library)

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

  // OTA/network runs on core 0; loop() stays free on core 1
  xTaskCreatePinnedToCore(apiotaTask, "apiota_ota", 8192, NULL, 2, NULL, 0);
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
