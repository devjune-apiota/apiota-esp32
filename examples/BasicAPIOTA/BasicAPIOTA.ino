/*
  ================================================================
   BasicAPIOTA — APIOTA Library Example
   ESP32 Dev Module (no display) — minimal + LED command
   ────────────────────────────────────────────────
   WiFi + provision + OTA + RSA/SHA verify + TLS(CA) + command poll
   all handled inside the library + logs every step over Serial
   add onCommand to receive led_on / led_off from the Dashboard
   add onConfig to receive Device Config values (v1.3.0+) — fires
   at boot + instantly every time you press Save Config
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

// values you can change live from Dashboard → ⚙️ Device Config (no reflash)
int blinkMs = 0;   // key "blink_ms" — 0 = LED steady, >0 = blink interval

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

  // Device Config (v1.3.0+) — set key "blink_ms" in Dashboard → ⚙️ Device Config → Save
  // the callback fires once at boot and instantly on every Save (no polling needed)
  APIOTA.onConfig([](const String& json){
    blinkMs = APIOTA.configGetInt("blink_ms", 0);   // default 0 when key not set
    Serial.printf("[CONFIG] blink_ms = %d (raw: %s)\n", blinkMs, json.c_str());
  });

  APIOTA.begin(API_KEY, FIRMWARE_VERSION, DEVICE_NAME);   // provision + OTA + poll task
}

void loop() {
  APIOTA.tick();   // keep OTA + command poll alive (also detects Approve / Unlock)

  // ── Approval / Lock gate (v1.4.0) ────────────────────────────────
  // hold the application until the owner presses ✓ Approve in the Dashboard,
  // and pause it whenever the device is Locked — resumes automatically
  if (!APIOTA.isApproved()) {
    digitalWrite(STATUS_LED, (millis() / 150) % 2);   // fast blink = waiting
    delay(100);
    return;                                           // application code does not run yet
  }

  // ── your application code below ──────────────────────────────────
  // apply the live config value — LED blinks at the rate set in Device Config
  if (blinkMs > 0) {
    digitalWrite(STATUS_LED, (millis() / blinkMs) % 2);
  }
  delay(20);   // keep loop responsive (no long delay)
}
