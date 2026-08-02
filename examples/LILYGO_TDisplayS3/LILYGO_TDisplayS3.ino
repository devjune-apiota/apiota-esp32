/*
  ================================================================
   LILYGO_TDisplayS3 — APIOTA full-UI example
   Board: LILYGO T-Display-S3 (ST7789 320x170, ESP32-S3)
   Libs : APIOTA | TFT_eSPI | WiFiManager (tzapu)
  ================================================================
   Display setup (once): open <Arduino libraries>/TFT_eSPI/User_Setup_Select.h
     1) comment out:  #include <User_Setup.h>
     2) uncomment:    #include <User_Setups/Setup206_LilyGo_T_Display_S3.h>

   Buttons:  BTN1 short = theme        BTN1 hold 5s = WiFi portal
             BTN2 short = OTA check    BTN1+BTN2 5s = erase WiFi + restart

   Prefer ~30 lines instead of full control? See LILYGO_TDisplayS3_Mini.
  ================================================================
*/

// ╔══════════════════════════════════════════════════════════════════╗
//   USER CONFIG — edit only this section
// ╠══════════════════════════════════════════════════════════════════╣
#define API_KEY                "ak_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"   // from apiota.net Dashboard
#define CURRENT_VERSION        "1.0.0"           // bump on every new build
#define DEVICE_NAME            "TDisplayS3"      // shown in Dashboard → Devices

#define OTA_CHECK_INTERVAL_SEC   60    // seconds between OTA checks (commands arrive instantly anyway; BTN2 = force check)
#define TELEMETRY_INTERVAL_SEC   20    // seconds between telemetry pushes to the Console (0 = off)
#define TFT_ROTATION             3     // 1 = normal, 3 = flipped 180
// ╚══════════════════════════════════════════════════════════════════╝

// ── INCLUDES ─────────────────────────────────────────────────────
#include "display_types.h"   // keep first (OTAStatus, Theme, ...)
#include <WiFi.h>
#include <WiFiManager.h>
#include <TFT_eSPI.h>
#if !defined(ST7789_DRIVER) || !defined(TFT_PARALLEL_8_BIT)   // wrong TFT_eSPI config = black screen -> fail the build with instructions
  #error "TFT_eSPI is NOT configured for LILYGO T-Display-S3! Fix (2 lines): open <Arduino libraries>/TFT_eSPI/User_Setup_Select.h -> comment out '#include <User_Setup.h>' -> uncomment '#include <User_Setups/Setup206_LilyGo_T_Display_S3.h>' -> re-upload. See the APIOTA README for details."
#endif
#include <APIOTA.h>

// ── PINS ─────────────────────────────────────────────────────────
#define TFT_BL_PIN  38
#define LED_PIN      2
#define BTN1_PIN    14
#define BTN2_PIN     0

// ── DISPLAY ──────────────────────────────────────────────────────
#define SCR_W  320
#define SCR_H  170

// ── GLOBALS ──────────────────────────────────────────────────────
TFT_eSPI             tft = TFT_eSPI();
static SemaphoreHandle_t g_stateMtx;
static SemaphoreHandle_t g_tftMtx;
static QueueHandle_t     g_dispQ;
static SharedState        g_st;
static TaskHandle_t       g_netTask  = nullptr;
static TaskHandle_t       g_ledTask  = nullptr;
static volatile bool      g_tasksStarted    = false;
static volatile bool      g_planLimitBlocked = false;
static char               DEVICE_ID[20]      = {0};
static uint32_t           CHIP_ID_CACHED     = 0;

APIOTAClient APIOTA;

#define ST_LOCK()    xSemaphoreTake(g_stateMtx, portMAX_DELAY)
#define ST_UNLOCK()  xSemaphoreGive(g_stateMtx)
#define TFT_LOCK()   xSemaphoreTake(g_tftMtx,   portMAX_DELAY)
#define TFT_UNLOCK() xSemaphoreGive(g_tftMtx)
#define DLY(ms)      vTaskDelay(pdMS_TO_TICKS(ms))

static uint32_t calcChipId() {
  uint32_t id = 0;
  for (int i = 0; i < 17; i += 8) id |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  return id;
}
static void deriveDeviceId() {
  CHIP_ID_CACHED = calcChipId();
  snprintf(DEVICE_ID, sizeof(DEVICE_ID), "ESP32_%06X", (unsigned)CHIP_ID_CACHED);
}

// ── FORWARD DECLARATIONS (needed by the helper files below) ──────
static void pushLog(const char* msg);
static void sendEv(OTAStatus st, int pct, const char* msg,
                   const char* nv = nullptr, bool rs = false, bool ra = false);
static void postDisplayEvent(const DisplayEvent& ev);
static void drawBase(uint8_t ti);
static void rAll(const SharedState& s, uint8_t ti);
static void rHeader(const SharedState& s, const Theme& T);
static void rBadge(const SharedState& s, const Theme& T);
static void rProgress(const SharedState& s, const Theme& T);
static void rStats(const SharedState& s, const Theme& T);
static void rLog(const SharedState& s, const Theme& T);
static void showBlockedScreen(const char* title, const char* line1,
                              const char* line2, const char* line3);
static bool wifiTryConnect(uint8_t startIdx = 0);
static bool wifiStartPortal(uint16_t timeoutSec = 180);
static void wifiMoveToFront(uint8_t idx);
static void wifiClearAll();
static void wifiAdd(const char* ssid, const char* pass);
static String jget(const String& js, const char* key);

#include "apiota_ui.h"     // themes + drawing
#include "apiota_wifi.h"   // saved-WiFi list + portal
#include "apiota_tasks.h"  // OTA + FreeRTOS tasks

// ================================================================
//  SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  deriveDeviceId();
  delay(50);

  Serial.printf("\n=== APIOTA Display  v%s  Device: %s ===\n", CURRENT_VERSION, DEVICE_ID);

  pinMode(LED_PIN, OUTPUT); pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP); pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH);
  pinMode(15, OUTPUT); digitalWrite(15, HIGH);   // T-Display-S3 PWR_EN (LCD power rail)

  g_stateMtx = xSemaphoreCreateMutex();
  g_tftMtx   = xSemaphoreCreateMutex();
  g_dispQ    = xQueueCreate(4, sizeof(DisplayEvent));
  memset(&g_st, 0, sizeof(g_st));
  g_st.blinkMs = 700;
  g_st.checkMs = (uint32_t)OTA_CHECK_INTERVAL_SEC * 1000UL;
  strncpy(g_st.logLines[2], "booting...", 63);

  tft.init(); tft.setRotation(TFT_ROTATION);
  tft.setViewport(0, 0, SCR_W, SCR_H); tft.setTextFont(1); tft.setTextSize(1);
  drawBase(0);

  // boot splash
  const Theme& T = THEMES[0];
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(T.accent, T.bg); tft.setTextSize(2); tft.drawString("APIOTA", SCR_W / 2, SCR_H / 2 - 16);
  tft.setTextSize(1); tft.setTextColor(T.muted, T.bg);
  tft.drawString("Display  " CURRENT_VERSION, SCR_W / 2, SCR_H / 2 + 2);
  tft.drawString(DEVICE_ID, SCR_W / 2, SCR_H / 2 + 14);
  delay(1500);

  // WiFi: saved networks first, captive portal if none work
  drawBase(0); wifiLoadAll();
  char bootMsg[48]; snprintf(bootMsg, sizeof(bootMsg), "%d saved WiFi found", g_wifiCount);
  pushLog(bootMsg);

  bool wifiOk = (g_wifiCount > 0) ? wifiTryConnect(0) : false;

  if (!wifiOk) {
    pushLog("opening WiFi portal...");
    WiFiManager wm; wm.setConfigPortalTimeout(120); wm.setConnectTimeout(15);
    if (wm.autoConnect("APIOTA-ESP32-SETUP")) {
      String savedSSID = wm.getWiFiSSID(), savedPass = wm.getWiFiPass();
      if (savedSSID.length() == 0) savedSSID = WiFi.SSID();
      if (savedPass.length() == 0) savedPass = WiFi.psk();
      if (savedSSID.length() == 0 || savedPass.length() == 0) {
        delay(300);
        if (savedSSID.length() == 0) savedSSID = WiFi.SSID();
        if (savedPass.length() == 0) savedPass = WiFi.psk();
      }
      if (savedSSID.length() > 0) wifiAdd(savedSSID.c_str(), savedPass.c_str());
      else pushLog("WARN: WiFi SSID empty, not saved");
    } else {
      tft.setTextDatum(MC_DATUM); tft.setTextColor(THEMES[0].red, THEMES[0].bg);
      tft.drawString("WiFi FAILED - restarting", SCR_W / 2, SCR_H / 2);
      vTaskDelay(pdMS_TO_TICKS(2000)); esp_restart();
    }
  }
  ST_LOCK(); g_st.wifiRssi = WiFi.RSSI(); g_st.freeHeapKB = ESP.getFreeHeap() / 1024; ST_UNLOCK();
  pushLog(("IP: " + WiFi.localIP().toString()).c_str());

  xTaskCreatePinnedToCore(taskDisplay,  "disp", 6144, nullptr, 3, nullptr,    1);
  xTaskCreatePinnedToCore(taskNetwork,  "net",  16384, nullptr, 2, &g_netTask, 0);
  xTaskCreatePinnedToCore(taskButton,   "btn",  8192, nullptr, 4, nullptr,    1);
  xTaskCreatePinnedToCore(taskLED,      "led",  2048, nullptr, 1, &g_ledTask, 1);
  xTaskCreatePinnedToCore(taskWatchdog, "wdog", 3072, nullptr, 1, nullptr,    0);

  g_tasksStarted = true;
  xTaskNotify(g_netTask, 0, eNoAction);
  Serial.println("=== APIOTA Display started ===");
}

void loop() {
  vTaskDelay(portMAX_DELAY);  // everything runs in FreeRTOS tasks
}
