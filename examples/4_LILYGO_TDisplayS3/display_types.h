#pragma once
// ================================================================
//  display_types.h — shared enums & structs for APIOTA Display
// ================================================================
#include <Arduino.h>

#define RGB565(r,g,b) (uint16_t)(((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3))

enum OTAStatus : uint8_t {
  OTA_IDLE, OTA_CHECKING, OTA_DOWNLOADING,
  OTA_VERIFYING, OTA_FLASHING,
  OTA_SUCCESS, OTA_FAILED, OTA_ROLLBACK
};

struct DisplayEvent {
  OTAStatus status;
  int       progress;   // -1 = no change
  char      log[64];
  char      newVer[16];
  bool      refreshStats;
  bool      refreshAll;
};

struct Theme {
  uint16_t bg, card, border;
  uint16_t text, muted;
  uint16_t accent, blue, amber, green, red, purple;
};

struct SharedState {
  OTAStatus otaStatus   = OTA_IDLE;
  int       otaProgress = 0;
  char      otaNewVer[16];
  int32_t   wifiRssi    = -99;
  uint32_t  freeHeapKB  = 0;
  uint32_t  uptimeMin   = 0;
  uint32_t  blinkMs     = 700;
  uint32_t  checkMs     = 60000;
  bool      debugMode   = false;
  char      logLines[3][64];
  uint8_t   theme       = 0;
  bool      forceCheck  = false;
  int       ledMode     = 0;   // 0=blink, 1=on, 2=off (command-controlled)
};
