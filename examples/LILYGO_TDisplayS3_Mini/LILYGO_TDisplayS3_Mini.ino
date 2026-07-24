/*
  ================================================================
   LILYGO_TDisplayS3_Mini — same on-screen UI, ~30 lines
   For: LILYGO T-Display-S3 (ST7789, 320x170, ESP32-S3)
  ================================================================
   The APIOTADisplay module does everything the big example does
   (TFT status UI + themes + WiFi captive portal + LED + buttons)
   in two objects. Want full control instead? See LILYGO_TDisplayS3.

   TFT_eSPI display setup — 2 lines, no pin typing needed:
     Open  <Arduino libraries>/TFT_eSPI/User_Setup_Select.h  then
       1) comment out:   #include <User_Setup.h>
       2) uncomment:     #include <User_Setups/Setup206_LilyGo_T_Display_S3.h>

   Dependencies: APIOTA | TFT_eSPI | WiFiManager (tzapu)
  ================================================================
*/
#include <APIOTA.h>
#include <APIOTADisplay.h>

#if !defined(ST7789_DRIVER) || !defined(TFT_PARALLEL_8_BIT)
  #error "TFT_eSPI is NOT configured for LILYGO T-Display-S3! Fix (2 lines): open <Arduino libraries>/TFT_eSPI/User_Setup_Select.h -> comment out '#include <User_Setup.h>' -> uncomment '#include <User_Setups/Setup206_LilyGo_T_Display_S3.h>' -> re-upload. See the APIOTA README for details."
#endif

// ╔════════════════════════════════════════════════════════════╗
//   USER CONFIG — edit only these two lines
// ╠════════════════════════════════════════════════════════════╣
#define API_KEY          "ak_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"   // from apiota.net Dashboard
#define CURRENT_VERSION  "1.0.0"                                                  // bump on every new build
// ╚════════════════════════════════════════════════════════════╝

APIOTAClient  APIOTA;
APIOTADisplay DISP;

void setup() {
  Serial.begin(115200);
  DISP.begin(CURRENT_VERSION);          // TFT + WiFi portal ("APIOTA-ESP32-SETUP") + tasks
  DISP.bindOTA(APIOTA);                 // OTA/provision/expired status appears on screen
  APIOTA.onCommand([](const String& c, const String& p, uint32_t id) {
    DISP.handleCommand(c, p);           // led_on/led_off/set_led/set_brightness/theme
  });
  APIOTA.begin(API_KEY, CURRENT_VERSION);
  DISP.setDeviceId(APIOTA.getDeviceId().c_str());
}

void loop() {
  APIOTA.tick();
  DISP.tick();
}
