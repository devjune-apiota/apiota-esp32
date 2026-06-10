// ================================================================
//  apiota_tasks.h — OTA functions + FreeRTOS tasks
//  included by the main .ino after the global variables are declared
// ================================================================
#pragma once

// ── APIOTA LIBRARY GLUE (B-05: migrated from raw HTTP to APIOTAClient) ──
//  Network / OTA / provisioning are now handled by the APIOTAClient library
//  (RSA verify, SHA-256, TLS-CA, NVS secret cache, command long-poll).
//  This file only maps library callbacks onto the existing TFT UI.

// Map library state enum -> display OTAStatus enum
static OTAStatus mapApiotaState(APIOTAState s) {
  switch (s) {
    case APST_DOWNLOADING:  return OTA_DOWNLOADING;
    case APST_VERIFYING:    return OTA_VERIFYING;
    case APST_FLASHING:     return OTA_FLASHING;
    case APST_SUCCESS:      return OTA_SUCCESS;
    case APST_FAILED:       return OTA_FAILED;
    case APST_ROLLBACK:     return OTA_ROLLBACK;
    case APST_CHECKING:
    case APST_PROVISIONING: return OTA_CHECKING;
    default:                return OTA_IDLE;
  }
}

// ── send generic telemetry to the 🖥 Console (heap / rssi / uptime / theme / version) ──
//   this board has no GPS — send generic values to show the relay works (add other sensors into data if you like)
static void sendTelemetryNow() {
  if (!APIOTA.isProvisioned() || WiFi.status() != WL_CONNECTED) return;
  ST_LOCK(); uint8_t th = g_st.theme; ST_UNLOCK();
  char data[160];
  snprintf(data, sizeof(data),
    "{\"heap_kb\":%u,\"rssi\":%d,\"uptime_min\":%u,\"theme\":%u,\"ver\":\"%s\"}",
    (unsigned)(ESP.getFreeHeap() / 1024), (int)WiFi.RSSI(),
    (unsigned)(millis() / 60000), (unsigned)th, CURRENT_VERSION);
  APIOTA.sendTelemetry(data);
}

// ================================================================
//  FREERTOS TASKS
// ================================================================

static void taskDisplay(void*) {
  DLY(200);
  { ST_LOCK(); SharedState snap0=g_st; uint8_t ti0=snap0.theme; ST_UNLOCK();
    TFT_LOCK(); drawBase(ti0); rAll(snap0,ti0); TFT_UNLOCK(); }
  DisplayEvent ev;
  TickType_t lastStat=xTaskGetTickCount();
  const TickType_t SINT=pdMS_TO_TICKS(10000);
  for (;;) {
    bool got=xQueueReceive(g_dispQ,&ev,pdMS_TO_TICKS(50))==pdTRUE;
    ST_LOCK(); SharedState snap=g_st; ST_UNLOCK();
    const Theme& T=THEMES[snap.theme];
    TFT_LOCK();
    if (got) {
      if (ev.refreshAll){ drawBase(snap.theme); rAll(snap,snap.theme); }
      else { rBadge(snap,T); rProgress(snap,T); rLog(snap,T); if(ev.refreshStats) rStats(snap,T); }
    }
    TickType_t now=xTaskGetTickCount();
    if ((now-lastStat)>=SINT){ lastStat=now; rHeader(snap,T); rStats(snap,T); }
    TFT_UNLOCK(); vTaskDelay(1);
  }
}

static void taskNetwork(void*) {
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  esp_ota_mark_app_valid_cancel_rollback();
  pushLog("firmware marked valid");

  // ── Configure APIOTA client ────────────────────────────────────
  APIOTA.setServer(OTA_SERVER);
  APIOTA.setCheckInterval(OTA_CHECK_INTERVAL_SEC);

  APIOTA.onProgress([](APIOTAState st, int pct, const char* msg) {
    sendEv(mapApiotaState(st), pct, msg);
  });
  APIOTA.onProvisioned([](const String& id, const String& sec) {
    strncpy(DEVICE_ID, id.c_str(), sizeof(DEVICE_ID) - 1); DEVICE_ID[sizeof(DEVICE_ID) - 1] = '\0';
    char m[48]; snprintf(m, sizeof(m), "provisioned: %s", DEVICE_ID);
    sendEv(OTA_IDLE, 0, m, nullptr, true, false);
  });
  APIOTA.onError([](const String& e) {
    if (e.indexOf("plan_limit") >= 0) {   // provision blocked: plan device quota exceeded (library reports via onError, not onExpired)
      g_planLimitBlocked = true;          // → taskNetwork begin()-retry waits 5 min (no rapid retry)
      showBlockedScreen("** DEVICE PLAN LIMIT **", "Plan limit exceeded", "Remove an old device in Dashboard", "or upgrade: apiota.net/pricing");
      return;
    }
    char m[64]; snprintf(m, sizeof(m), "%.60s", e.c_str()); sendEv(OTA_FAILED, -1, m);
  });
  APIOTA.onCommand([](const String& cmd, const String& payload, uint32_t id) {
    char m[48]; snprintf(m, sizeof(m), "cmd: %.40s", cmd.c_str());
    sendEv(OTA_IDLE, -1, m);   // show the command on screen (log line) + redraw
    if (cmd == "led_on")       { ST_LOCK(); g_st.ledMode = 1; ST_UNLOCK(); }
    else if (cmd == "led_off") { ST_LOCK(); g_st.ledMode = 2; ST_UNLOCK(); }
    else if (cmd == "set_led") {                 // payload {"blink":ms}
      uint32_t b = (uint32_t)jget(payload, "blink").toInt(); if (b < 50) b = 700;
      ST_LOCK(); g_st.ledMode = 0; g_st.blinkMs = b; ST_UNLOCK();
      if (g_ledTask) xTaskNotify(g_ledTask, b, eSetValueWithOverwrite);
    } else if (cmd == "set_brightness") {        // payload {"value":0-255}
      int v = jget(payload, "value").toInt(); if (v < 0) v = 0; if (v > 255) v = 255;
      analogWrite(TFT_BL_PIN, v);
    } else if (cmd == "theme") {                 // payload {"value":0-2}
      int t = jget(payload, "value").toInt(); if (t < 0 || t > 2) t = 0;
      ST_LOCK(); g_st.theme = (uint8_t)t; uint8_t ti = g_st.theme; ST_UNLOCK();
      TFT_LOCK(); drawBase(ti); TFT_UNLOCK();
      DisplayEvent ev = {}; ev.status = OTA_IDLE; ev.progress = -1; ev.refreshAll = true; postDisplayEvent(ev);
    }
    // reboot & check_update handled inside the library automatically
  });
  // Server told the device to stop (lift-time / working-lifetime / plan / blocked)
  APIOTA.onExpired([](APIOTAExpiry reason, const String& msg) {
    g_planLimitBlocked = true;
    switch (reason) {
      case APEX_LIFT_TIME:
        showBlockedScreen("** DEVICE EXPIRED **", "Device Lift Time expired", "Renew at apiota.net", "Management > Device Lift Time"); break;
      case APEX_WORKING:
        showBlockedScreen("** DEVICE EXPIRED **", "Working Lifetime expired", "Renew at apiota.net", "Management > Working Lifetime"); break;
      case APEX_PLAN_LIMIT:
        showBlockedScreen("** DEVICE PLAN LIMIT **", "Plan limit exceeded", "Remove an old device in Dashboard", "or upgrade: apiota.net/pricing"); break;
      default:
        showBlockedScreen("** DEVICE BLOCKED **", "Access denied by server", "Check the API Key in Dashboard", "apiota.net"); break;
    }
  });

  // ── Wait for WiFi, then provision/validate (library caches secret in NVS) ──
  while (WiFi.status() != WL_CONNECTED) { pushLog("waiting WiFi for provision..."); DLY(2000); }
  while (!APIOTA.begin(API_KEY, CURRENT_VERSION, DEVICE_ID)) {
    if (g_planLimitBlocked) { pushLog("BLOCKED: waiting 5min..."); DLY(300000); g_planLimitBlocked = false; }
    else                    { pushLog("provision failed, retry 30s..."); DLY(30000); }
    while (WiFi.status() != WL_CONNECTED) DLY(1000);
  }

  // ── Steady state: tick() auto-checks OTA; BTN2/watchdog notify -> force check ──
  uint32_t lastStat = 0, lastTele = 0;
  sendTelemetryNow();                    // send the first values right after provision → Console shows data immediately
  for (;;) {
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) > 0) APIOTA.forceCheck();
    APIOTA.tick();
    ST_LOCK(); g_st.forceCheck = false; g_st.wifiRssi = WiFi.RSSI();
             g_st.freeHeapKB = ESP.getFreeHeap() / 1024; g_st.uptimeMin = millis() / 60000; ST_UNLOCK();
    uint32_t now = millis();
    if (now - lastStat >= 10000) { lastStat = now;
      DisplayEvent se = {}; se.status = OTA_IDLE; se.progress = -1; se.refreshStats = true; postDisplayEvent(se); }
#if TELEMETRY_INTERVAL_SEC > 0
    if (now - lastTele >= (uint32_t)TELEMETRY_INTERVAL_SEC * 1000UL) { lastTele = now; sendTelemetryNow(); }
#endif
  }
}

static void taskLED(void*) {
  uint32_t ms=700; bool st=false;
  for (;;) {
    uint32_t notifiedValue=0;
    if (xTaskNotifyWait(0,0xFFFFFFFF,&notifiedValue,pdMS_TO_TICKS(ms))==pdTRUE) {
      if (notifiedValue>0) ms=notifiedValue; else { ST_LOCK(); ms=g_st.blinkMs; ST_UNLOCK(); }
    }
    int mode; ST_LOCK(); mode=g_st.ledMode; ST_UNLOCK();
    if (mode==1)      digitalWrite(LED_PIN,HIGH);   // led_on
    else if (mode==2) digitalWrite(LED_PIN,LOW);    // led_off
    else { st=!st; digitalWrite(LED_PIN,st); }      // blink (default)
  }
}

static void taskButton(void*) {
  bool p1=HIGH,p2=HIGH;
  uint32_t btn1DownAt=0; bool btn1Held=false,btn1LongDone=false; uint8_t btn1Countdown=255;
  uint32_t btn2DownAt=0; bool btn2Held=false;
  uint32_t comboDownAt=0; bool comboActive=false,comboTriggered=false; uint8_t comboCountdown=255;
  const uint32_t PORTAL_MS=5000, COMBO_MS=5000;
  for (;;) {
    uint32_t now=xTaskGetTickCount()*portTICK_PERIOD_MS;
    bool b1=digitalRead(BTN1_PIN),b2=digitalRead(BTN2_PIN);
    // COMBO BTN1+BTN2
    if (b1==LOW&&b2==LOW) {
      if (!comboActive){ comboActive=true; comboDownAt=now; comboTriggered=false; comboCountdown=255; btn1LongDone=true; btn2Held=false; }
      if (!comboTriggered){
        uint32_t held=now-comboDownAt; uint8_t secsLeft=(held<COMBO_MS)?(uint8_t)((COMBO_MS-held)/1000):0;
        if (secsLeft!=comboCountdown){ comboCountdown=secsLeft; char m[48]; if(secsLeft>0) snprintf(m,sizeof(m),"CLEAR WiFi in %ds...",secsLeft); else snprintf(m,sizeof(m),"CLEARING ALL WiFi..."); pushLog(m); }
        if (held>=COMBO_MS){ comboTriggered=true; sendEv(OTA_FAILED,100,"WiFi cleared! Restarting...");
          ST_LOCK(); uint8_t ti=g_st.theme; ST_UNLOCK(); const Theme& T=THEMES[ti];
          TFT_LOCK(); tft.fillRect(0,40,SCR_W,90,T.bg); tft.setTextDatum(MC_DATUM); tft.setTextSize(1);
          tft.setTextColor(T.red,T.bg); tft.drawString("** ALL WiFi CLEARED **",SCR_W/2,65);
          tft.setTextColor(T.amber,T.bg); tft.drawString("Restarting in 2s...",SCR_W/2,80); TFT_UNLOCK();
          wifiClearAll(); DLY(2000); esp_restart(); }
      }
    } else { if(comboActive&&!comboTriggered) pushLog("combo cancelled"); comboActive=false; comboTriggered=false; }
    if (comboActive){ p1=b1; p2=b2; vTaskDelay(pdMS_TO_TICKS(10)); continue; }
    // BTN1
    if (b1==LOW&&p1==HIGH){ btn1DownAt=now; btn1Held=true; btn1LongDone=false; btn1Countdown=255; }
    if (b1==LOW&&btn1Held&&!btn1LongDone){
      uint32_t held=now-btn1DownAt;
      if (held>=2000){
        uint8_t secsLeft=(held<PORTAL_MS)?(uint8_t)((PORTAL_MS-held)/1000):0;
        if (secsLeft!=btn1Countdown){ btn1Countdown=secsLeft; char m[52]; if(secsLeft>0) snprintf(m,sizeof(m),"WiFi Setup in %ds...",secsLeft); else snprintf(m,sizeof(m),"Opening WiFi Manager..."); pushLog(m); }
      }
      if (held>=PORTAL_MS){ btn1LongDone=true; btn1Held=false; wifiStartPortal(180);
        if (WiFi.status()==WL_CONNECTED){ ST_LOCK(); g_st.wifiRssi=WiFi.RSSI(); g_st.freeHeapKB=ESP.getFreeHeap()/1024; ST_UNLOCK(); if(g_netTask) xTaskNotify(g_netTask,1,eSetValueWithOverwrite); }
      }
    }
    if (b1==HIGH&&p1==LOW&&btn1Held){ uint32_t held=now-btn1DownAt; btn1Held=false;
      if (!btn1LongDone&&held<PORTAL_MS){
        ST_LOCK(); g_st.theme=(g_st.theme+1)%3; uint8_t ti=g_st.theme; ST_UNLOCK();
        TFT_LOCK(); drawBase(ti); TFT_UNLOCK();
        DisplayEvent ev={}; ev.status=OTA_IDLE; ev.progress=-1; ev.refreshAll=true; postDisplayEvent(ev);
        const char* nm[]={"dark","light","blue-hud"}; char m[32]; snprintf(m,sizeof(m),"theme: %s",nm[ti]); pushLog(m);
      }
    }
    // BTN2
    if (b2==LOW&&p2==HIGH){ btn2DownAt=now; btn2Held=true; }
    if (b2==HIGH&&p2==LOW&&btn2Held){ btn2Held=false; ST_LOCK(); g_st.forceCheck=true; ST_UNLOCK(); if(g_netTask) xTaskNotify(g_netTask,1,eSetValueWithOverwrite); pushLog("btn2: force OTA check"); }
    p1=b1; p2=b2; vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void taskWatchdog(void*) {
  uint8_t disconnectCount=0;
  for (;;) {
    DLY(5000);
    uint32_t heap=esp_get_free_heap_size();
    if (heap<30000){ char m[40]; snprintf(m,sizeof(m),"WARN: heap low %dKB",heap/1024); pushLog(m); }
    if (g_portalActive) continue;
    if (WiFi.status()!=WL_CONNECTED){
      disconnectCount++;
      char m[50]; snprintf(m,sizeof(m),"WiFi lost (x%d) - reconnecting",disconnectCount); pushLog(m);
      if (g_wifiCurrent<g_wifiCount&&g_wifiList[g_wifiCurrent].ssid[0])
        WiFi.begin(g_wifiList[g_wifiCurrent].ssid,g_wifiList[g_wifiCurrent].pass);
      else WiFi.reconnect();
      DLY(8000);
      if (WiFi.status()==WL_CONNECTED){
        disconnectCount=0; if(g_wifiCurrent>0) wifiMoveToFront(g_wifiCurrent);
        pushLog("WiFi reconnected"); ST_LOCK(); g_st.wifiRssi=WiFi.RSSI(); ST_UNLOCK();
        DisplayEvent ev={}; ev.status=OTA_IDLE; ev.progress=-1; ev.refreshStats=true; postDisplayEvent(ev);
        continue;
      }
      if (g_wifiCount>1){ snprintf(m,sizeof(m),"trying other WiFi (%d saved)...",g_wifiCount); pushLog(m);
        if (wifiTryConnect((g_wifiCurrent+1)%g_wifiCount)){
          disconnectCount=0; char ok[48]; snprintf(ok,sizeof(ok),"switched to: %s",g_wifiList[g_wifiCurrent].ssid); pushLog(ok);
          ST_LOCK(); g_st.wifiRssi=WiFi.RSSI(); g_st.freeHeapKB=ESP.getFreeHeap()/1024; ST_UNLOCK();
          if(g_netTask) xTaskNotify(g_netTask,1,eSetValueWithOverwrite);
          DisplayEvent ev={}; ev.status=OTA_IDLE; ev.progress=-1; ev.refreshStats=true; postDisplayEvent(ev);
        } else pushLog("all saved WiFi unreachable");
      }
      if (disconnectCount>=5&&g_wifiCount==0){ pushLog("auto-opening WiFi portal..."); wifiStartPortal(120); disconnectCount=0; }
    } else disconnectCount=0;
  }
}
