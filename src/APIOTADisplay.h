// ═══════════════════════════════════════════════════════════════════
//   APIOTADisplay — TFT OTA monitor for LILYGO T-Display-S3 (+ ST7789)
//   v1.0.0  ·  apiota.net  ·  header-only companion to APIOTAClient
//   ───────────────────────────────────────────────────────────────
//   Includes: TFT monitor UI + themes + WiFiManager + FreeRTOS tasks (display/led/button)
//        + binds to APIOTAClient (auto-shows provision/OTA/expired on screen)
//
//   Usage:
//     #include <APIOTA.h>
//     #include <APIOTADisplay.h>
//     APIOTAClient  APIOTA;
//     APIOTADisplay DISP;
//     void setup(){
//       DISP.begin("1.0.0");                 // init TFT + tasks + WiFiManager
//       DISP.bindOTA(APIOTA);                // shows OTA status on screen for you
//       APIOTA.onCommand([](const String& c,const String& p,uint32_t id){
//         DISP.handleCommand(c,p);           // led_on/led_off/set_led/set_brightness/theme
//       });
//       APIOTA.begin("ak_xxx","1.0.0","MyDevice");
//     }
//     void loop(){ APIOTA.tick(); DISP.tick(); }
//
//   Configure TFT_eSPI User_Setup.h for your board (ST7789 320x170, T-Display-S3 pins)
//   Dependencies: TFT_eSPI · WiFiManager (tzapu) · APIOTA
// ═══════════════════════════════════════════════════════════════════
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <TFT_eSPI.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <APIOTA.h>   // APIOTAClient, APIOTAState, APIOTAExpiry

// ── Pins (can be overridden before include) ───────────────────────
#ifndef APIOTADISP_TFT_BL
  #define APIOTADISP_TFT_BL  38
#endif
#ifndef APIOTADISP_LED
  #define APIOTADISP_LED      2
#endif
#ifndef APIOTADISP_BTN1
  #define APIOTADISP_BTN1    14
#endif
#ifndef APIOTADISP_BTN2
  #define APIOTADISP_BTN2     0
#endif
#ifndef APIOTADISP_W
  #define APIOTADISP_W      320
#endif
#ifndef APIOTADISP_H
  #define APIOTADISP_H      170
#endif

#define APIOTADISP_RGB(r,g,b) (uint16_t)(((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3))

struct APIOTADispTheme {
  uint16_t bg, card, border, text, muted, accent, blue, amber, green, red, purple;
};

class APIOTADisplay {
public:
  // ── Public API ───────────────────────────────────────────────────
  void begin(const char* fwVersion, uint8_t rotation = 3,
             const char* portalAP = "APIOTA-ESP32-SETUP",
             uint16_t connectTimeoutSec = 15, uint16_t portalTimeoutSec = 180) {
    _fwVer = fwVersion ? fwVersion : "0.0.0";
    _deriveDeviceId();

    pinMode(APIOTADISP_LED, OUTPUT);  digitalWrite(APIOTADISP_LED, LOW);
    pinMode(APIOTADISP_BTN1, INPUT_PULLUP);
    pinMode(APIOTADISP_BTN2, INPUT_PULLUP);
    pinMode(APIOTADISP_TFT_BL, OUTPUT); digitalWrite(APIOTADISP_TFT_BL, HIGH);

    _stateMtx = xSemaphoreCreateMutex();
    _tftMtx   = xSemaphoreCreateMutex();
    _dispQ    = xQueueCreate(4, sizeof(_Ev));

    _tft.init(); _tft.setRotation(rotation);
    _tft.setViewport(0, 0, APIOTADISP_W, APIOTADISP_H);
    _tft.setTextFont(1); _tft.setTextSize(1);
    _drawBase(0);

    // splash
    const APIOTADispTheme& T = _THEMES[0];
    _tft.setTextDatum(MC_DATUM);
    _tft.setTextColor(T.accent, T.bg); _tft.setTextSize(2);
    _tft.drawString("APIOTA", APIOTADISP_W/2, APIOTADISP_H/2-16);
    _tft.setTextSize(1); _tft.setTextColor(T.muted, T.bg);
    _tft.drawString((String("Display v")+_fwVer).c_str(), APIOTADISP_W/2, APIOTADISP_H/2+2);
    _tft.drawString(_deviceId.c_str(), APIOTADISP_W/2, APIOTADISP_H/2+14);
    delay(1500);
    _drawBase(0);

    // WiFi (saved or captive portal)
    _log("connecting WiFi...");
    WiFiManager wm; wm.setConfigPortalTimeout(portalTimeoutSec); wm.setConnectTimeout(connectTimeoutSec);
    if (!wm.autoConnect(portalAP)) {
      _log("WiFi failed, restarting");
      delay(2000); ESP.restart();
    }
    _st.wifiRssi = WiFi.RSSI();
    _log(("IP: " + WiFi.localIP().toString()).c_str());

    _tasksStarted = true;
    xTaskCreatePinnedToCore(&APIOTADisplay::_dispTramp, "ap_disp", 6144, this, 3, &_dispTask, 1);
    xTaskCreatePinnedToCore(&APIOTADisplay::_ledTramp,  "ap_led",  2048, this, 1, &_ledTask,  1);
    xTaskCreatePinnedToCore(&APIOTADisplay::_btnTramp,  "ap_btn",  4096, this, 4, &_btnTask,  1);
  }

  // call in loop() — periodically updates stats
  void tick() {
    uint32_t now = millis();
    if (now - _lastStat >= 5000) {
      _lastStat = now;
      _stLock(); _st.wifiRssi = WiFi.RSSI(); _st.freeHeapKB = ESP.getFreeHeap()/1024;
                 _st.uptimeMin = millis()/60000; _stUnlock();
      _Ev e = {}; e.status = _st.status; e.progress = -1; e.refreshStats = true; _post(e);
    }
  }

  // bind to APIOTAClient → auto-shows provision/OTA/expired on screen
  void bindOTA(APIOTAClient& ota) {
    _ota = &ota;
    ota.onProgress([this](APIOTAState st, int pct, const char* msg){ _onProgress(st, pct, msg); });
    ota.onProvisioned([this](const String& id, const String& sec){
      char m[48]; snprintf(m, sizeof(m), "provisioned: %s", id.c_str());
      _deviceId = id; _sendEv(APST_IDLE, 0, m, true, false);
    });
    ota.onError([this](const String& e){ char m[64]; snprintf(m,sizeof(m),"%.60s",e.c_str()); _sendEv(APST_FAILED,-1,m,false,false); });
    ota.onExpired([this](APIOTAExpiry r, const String& msg){
      if (r == APEX_BLOCKED) {
        _showBlocked("** DEVICE LOCKED **", "locked by owner", "unlock in Dashboard", msg.c_str());
        return;
      }
      const char* t = r==APEX_LIFT_TIME ? "Device Lift Time expired" :
                      r==APEX_WORKING   ? "Working Lifetime expired" :
                      r==APEX_PLAN_LIMIT? "Plan device limit reached" : "Device stopped";
      _showBlocked("** DEVICE STOPPED **", t, "renew at apiota.net", msg.c_str());
    });
  }

  // call directly from APIOTA.onCommand() — ready-made led/brightness/theme control
  bool handleCommand(const String& cmd, const String& payload) {
    char m[48]; snprintf(m, sizeof(m), "cmd: %.40s", cmd.c_str()); _sendEv(_st.status, -1, m, false, false);
    if (cmd == "led_on")  { _stLock(); _ledMode = 1; _stUnlock(); return true; }
    if (cmd == "led_off") { _stLock(); _ledMode = 2; _stUnlock(); return true; }
    if (cmd == "set_led") { uint32_t b=(uint32_t)_jnum(payload,"blink"); if(b<50)b=700; _stLock(); _ledMode=0; _blinkMs=b; _stUnlock(); return true; }
    if (cmd == "set_brightness") { int v=(int)_jnum(payload,"value"); if(v<0)v=0; if(v>255)v=255; analogWrite(APIOTADISP_TFT_BL, v); return true; }
    if (cmd == "theme") { int t=(int)_jnum(payload,"value"); if(t<0||t>2)t=0; _setTheme((uint8_t)t); return true; }
    return false;  // unknown — let the sketch handle it
  }

  void log(const char* msg) { _sendEv(_st.status, -1, msg, false, false); }
  void setDeviceId(const char* id) { if (id && *id) _deviceId = id; }
  const String& deviceId() const { return _deviceId; }

private:
  // ── State ────────────────────────────────────────────────────────
  struct _State {
    APIOTAState status = APST_IDLE;
    int      progress  = 0;
    char     newVer[16] = {0};
    int32_t  wifiRssi  = -99;
    uint32_t freeHeapKB = 0;
    uint32_t uptimeMin  = 0;
    uint8_t  theme     = 0;
    char     logLines[3][64] = {{0},{0},{0}};
  };
  struct _Ev { APIOTAState status; int progress; char log[64]; char newVer[16]; bool refreshStats; bool refreshAll; };

  TFT_eSPI _tft = TFT_eSPI();
  SemaphoreHandle_t _stateMtx = nullptr, _tftMtx = nullptr;
  QueueHandle_t     _dispQ = nullptr;
  TaskHandle_t      _dispTask = nullptr, _ledTask = nullptr, _btnTask = nullptr;
  volatile bool     _tasksStarted = false;
  APIOTAClient*     _ota = nullptr;
  _State            _st;
  String            _fwVer = "0.0.0";
  String            _deviceId;
  uint32_t          _blinkMs = 700;
  int               _ledMode = 0;   // 0=blink 1=on 2=off
  uint32_t          _lastStat = 0;

  static const APIOTADispTheme _THEMES[3];

  void _stLock()   { xSemaphoreTake(_stateMtx, portMAX_DELAY); }
  void _stUnlock() { xSemaphoreGive(_stateMtx); }
  void _tftLock()  { xSemaphoreTake(_tftMtx, portMAX_DELAY); }
  void _tftUnlock(){ xSemaphoreGive(_tftMtx); }

  void _deriveDeviceId() {
    uint32_t id = 0;
    for (int i = 0; i < 17; i += 8) id |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
    char buf[20]; snprintf(buf, sizeof(buf), "ESP32_%06X", (unsigned)id);
    if (_deviceId.length() == 0) _deviceId = buf;
  }

  // tiny JSON number getter:  {"key": 123}
  static long _jnum(const String& js, const char* key) {
    String n = String("\"") + key + "\"";
    int i = js.indexOf(n); if (i < 0) return 0;
    i = js.indexOf(':', i); if (i < 0) return 0;
    return js.substring(i + 1).toInt();
  }

  void _post(const _Ev& ev) {
    if (xQueueSend(_dispQ, &ev, 0) != pdTRUE) { _Ev d; xQueueReceive(_dispQ,&d,0); xQueueSend(_dispQ,&ev,0); }
  }

  void _log(const char* msg) {
    _stLock();
    strncpy(_st.logLines[0],_st.logLines[1],63);
    strncpy(_st.logLines[1],_st.logLines[2],63);
    strncpy(_st.logLines[2],msg,63); _st.logLines[2][63]='\0';
    _State snap=_st; _stUnlock();
    Serial.printf("[DISP] %s\n", msg);
    if (!_tasksStarted) { _tftLock(); _rLog(snap); _tftUnlock(); }
  }

  void _sendEv(APIOTAState st, int pct, const char* msg, bool rs, bool ra) {
    _stLock();
    _st.status = st;
    if (pct >= 0) _st.progress = pct;
    strncpy(_st.logLines[0],_st.logLines[1],63);
    strncpy(_st.logLines[1],_st.logLines[2],63);
    strncpy(_st.logLines[2],msg,63); _st.logLines[2][63]='\0';
    _stUnlock();
    _Ev ev = {}; ev.status=st; ev.progress=pct; ev.refreshStats=rs; ev.refreshAll=ra;
    strncpy(ev.log,msg,63);
    _post(ev);
    Serial.printf("[DISP] %s\n", msg);
  }

  void _onProgress(APIOTAState st, int pct, const char* msg) {
    if (msg && strstr(msg, "unlocked")) { _sendEv(st, pct, msg, true, true); return; }  // unlock -> full screen redraw
    if (st == APST_CHECKING && msg && strstr(msg, "new version")) {
      // capture target version for the badge
      _stLock(); const char* v = strrchr(msg, ' '); if (v) { strncpy(_st.newVer, v+1, 15); _st.newVer[15]='\0'; } _stUnlock();
    }
    _sendEv(st, pct, msg ? msg : "", false, false);
  }

  void _setTheme(uint8_t ti) {
    _stLock(); _st.theme = ti; _stUnlock();
    _tftLock(); _drawBase(ti); _tftUnlock();
    _Ev e = {}; e.status=_st.status; e.progress=-1; e.refreshAll=true; _post(e);
  }

  // ── Drawing (uses _tft, _st, _fwVer, _deviceId) ───────────────────
  static uint16_t _stColor(APIOTAState st, const APIOTADispTheme& T) {
    switch (st) {
      case APST_IDLE:        return T.muted;
      case APST_CHECKING:    return T.blue;
      case APST_DOWNLOADING: return T.amber;
      case APST_VERIFYING:   return T.purple;
      case APST_FLASHING:    return T.amber;
      case APST_SUCCESS:     return T.green;
      case APST_FAILED:      return T.red;
      case APST_ROLLBACK:    return T.amber;
      default:               return T.muted;
    }
  }
  static const char* _stLabel(APIOTAState st) {
    switch (st) {
      case APST_IDLE: return "IDLE"; case APST_PROVISIONING: return "PROVISION";
      case APST_CHECKING: return "CHECKING"; case APST_DOWNLOADING: return "DOWNLOAD";
      case APST_VERIFYING: return "VERIFY"; case APST_FLASHING: return "FLASHING";
      case APST_SUCCESS: return "SUCCESS"; case APST_FAILED: return "FAILED";
      case APST_ROLLBACK: return "ROLLBACK"; default: return "?";
    }
  }
  void _pbar(int x,int y,int w,int h,int pct,uint16_t fc,uint16_t bc,uint16_t boc){
    _tft.fillRoundRect(x,y,w,h,3,bc);
    if(pct>0) _tft.fillRoundRect(x,y,max(1,min(w,w*pct/100)),h,3,fc);
    _tft.drawRoundRect(x,y,w,h,3,boc);
  }
  void _card(int x,int y,int w,int h,const char* lbl,const char* val,uint16_t vc,const APIOTADispTheme& T){
    _tft.fillRoundRect(x,y,w,h,3,T.card); _tft.drawRoundRect(x,y,w,h,3,T.border);
    _tft.setTextDatum(TC_DATUM); _tft.setTextSize(1);
    _tft.setTextColor(T.muted,T.card); _tft.drawString(lbl,x+w/2,y+5);
    _tft.setTextColor(vc,T.card); _tft.drawString(val,x+w/2,y+17);
  }
  void _wifiIcon(int x,int y,int rssi,const APIOTADispTheme& T){
    int b=(rssi>-50)?4:(rssi>-60)?3:(rssi>-70)?2:1;
    for(int i=0;i<4;i++){int bh=(i+1)*3; _tft.fillRect(x+i*5,y+12-bh,4,bh,(i<b)?T.accent:T.muted);}
  }
  void _drawBase(uint8_t ti) {
    const APIOTADispTheme& T = _THEMES[ti];
    _tft.fillScreen(T.bg);
    _tft.fillRect(0,0,APIOTADISP_W,28,T.card);
    _tft.drawFastHLine(0,27,APIOTADISP_W,T.border);
    _tft.fillRoundRect(5,5,50,16,3,T.accent);
    _tft.setTextDatum(ML_DATUM); _tft.setTextSize(1);
    _tft.setTextColor(TFT_BLACK,T.accent); _tft.drawString("APIOTA",10,13);
    _tft.setTextColor(T.text,T.card);
    String did=_deviceId; if(did.length()>14) did=did.substring(0,14);
    _tft.drawString(did.c_str(),62,13);
    _tft.drawFastHLine(0,138,APIOTADISP_W,T.border);
  }
  void _rHeader(const _State& s,const APIOTADispTheme& T){
    _tft.fillRect(APIOTADISP_W-126,0,126,28,T.card);
    _wifiIcon(APIOTADISP_W-122,7,s.wifiRssi,T);
    char b[16]; snprintf(b,sizeof(b),"%ddBm",(int)s.wifiRssi);
    _tft.setTextDatum(TL_DATUM); _tft.setTextColor(T.muted,T.card); _tft.setTextSize(1);
    _tft.drawString(b, APIOTADISP_W-94, 3);
    _tft.drawString(WiFi.localIP().toString().c_str(), APIOTADISP_W-94, 14);
  }
  void _rBadge(const _State& s,const APIOTADispTheme& T){
    uint16_t sc=_stColor(s.status,T);
    int bx=APIOTADISP_W-74, by=32, bw=68, bh=16;
    _tft.fillRect(bx-2,by-2,bw+4,bh+4,T.bg);
    _tft.drawRoundRect(bx,by,bw,bh,3,sc);
    _tft.setTextDatum(MC_DATUM); _tft.setTextColor(sc,T.bg); _tft.setTextSize(1);
    _tft.drawString(_stLabel(s.status),bx+bw/2,by+bh/2);
    _tft.fillRect(6,30,APIOTADISP_W-82,28,T.bg);
    _tft.setTextDatum(ML_DATUM); _tft.setTextColor(T.muted,T.bg);
    _tft.drawString("OTA UPDATE",6,37);
    char vb[40];
    if (s.newVer[0]) snprintf(vb,sizeof(vb),"v%s -> v%s",_fwVer.c_str(),s.newVer);
    else             snprintf(vb,sizeof(vb),"v%s",_fwVer.c_str());
    _tft.setTextColor(T.text,T.bg); _tft.drawString(vb,6,49);
  }
  void _rProgress(const _State& s,const APIOTADispTheme& T){
    uint16_t pc=_stColor(s.status,T);
    _pbar(6,60,APIOTADISP_W-96,10,s.progress,pc,T.card,T.border);
    _tft.fillRect(APIOTADISP_W-82,56,76,16,T.bg);
    _tft.setTextDatum(ML_DATUM); _tft.setTextColor(pc,T.bg);
    char b[8]; snprintf(b,sizeof(b),"%d%%",s.progress);
    _tft.drawString(b,APIOTADISP_W-76,66);
  }
  void _rStats(const _State& s,const APIOTADispTheme& T){
    char hb[10],ub[12];
    snprintf(hb,sizeof(hb),"%dKB",(int)s.freeHeapKB);
    if (s.uptimeMin>=60) snprintf(ub,sizeof(ub),"%dh%02dm",(int)(s.uptimeMin/60),(int)(s.uptimeMin%60));
    else                 snprintf(ub,sizeof(ub),"%dm",(int)s.uptimeMin);
    char vs[10]; snprintf(vs,sizeof(vs),"v%s",_fwVer.c_str());
    char rb[10]; snprintf(rb,sizeof(rb),"%ddB",(int)s.wifiRssi);
    _card(6,  78,74,36,"HEAP",  hb,(s.freeHeapKB>100)?T.green:T.amber,T);
    _card(82, 78,74,36,"UPTIME",ub,T.blue,T);
    _card(158,78,74,36,"RSSI",  rb,(s.wifiRssi>-65)?T.green:(s.wifiRssi>-75?T.amber:T.red),T);
    _card(234,78,80,36,"VER",   vs,T.accent,T);
    _tft.fillRect(6,118,APIOTADISP_W-12,14,T.bg);
    _tft.setTextDatum(TL_DATUM); _tft.setTextColor(T.muted,T.bg); _tft.setTextSize(1);
    _tft.drawString(("IP: "+WiFi.localIP().toString()).c_str(),8,120);
  }
  void _rLog(const _State& s){
    const APIOTADispTheme& T=_THEMES[s.theme];
    uint16_t lc=_stColor(s.status,T);
    _tft.fillRect(0,140,APIOTADISP_W,APIOTADISP_H-140,T.card);
    uint16_t cols[3]={T.muted,T.muted,lc}; int ys[3]={143,152,161};
    for(int i=0;i<3;i++){ if(!s.logLines[i][0])continue;
      _tft.setTextDatum(TL_DATUM); _tft.setTextColor(cols[i],T.card); _tft.setTextSize(1);
      _tft.drawString(s.logLines[i],4,ys[i]); }
  }
  void _rAll(const _State& s){ const APIOTADispTheme& T=_THEMES[s.theme]; _rHeader(s,T); _rBadge(s,T); _rProgress(s,T); _rStats(s,T); _rLog(s); }
  void _showBlocked(const char* title,const char* l1,const char* l2,const char* l3){
    _stLock(); uint8_t ti=_st.theme; _stUnlock();
    const APIOTADispTheme& T=_THEMES[ti];
    _tftLock();
    _tft.fillRect(0,28,APIOTADISP_W,110,T.bg); _tft.setTextDatum(MC_DATUM); _tft.setTextSize(1);
    _tft.drawRoundRect(4,32,APIOTADISP_W-8,102,4,T.red);
    _tft.setTextColor(T.red,T.bg);   _tft.drawString(title,APIOTADISP_W/2,48);
    _tft.setTextColor(T.amber,T.bg); if(l1&&l1[0]) _tft.drawString(l1,APIOTADISP_W/2,68);
    _tft.setTextColor(T.text,T.bg);  if(l2&&l2[0]) _tft.drawString(l2,APIOTADISP_W/2,86);
    _tft.setTextColor(T.muted,T.bg); if(l3&&l3[0]) _tft.drawString(l3,APIOTADISP_W/2,104);
    _tftUnlock();
  }

  // ── FreeRTOS tasks (trampolines → instance methods) ───────────────
  static void _dispTramp(void* a){ static_cast<APIOTADisplay*>(a)->_dispRun(); }
  static void _ledTramp(void* a){ static_cast<APIOTADisplay*>(a)->_ledRun(); }
  static void _btnTramp(void* a){ static_cast<APIOTADisplay*>(a)->_btnRun(); }

  void _dispRun() {
    vTaskDelay(pdMS_TO_TICKS(200));
    { _stLock(); _State snap=_st; _stUnlock(); _tftLock(); _drawBase(snap.theme); _rAll(snap); _tftUnlock(); }
    _Ev ev; TickType_t lastStat=xTaskGetTickCount();
    for (;;) {
      bool got = xQueueReceive(_dispQ,&ev,pdMS_TO_TICKS(50))==pdTRUE;
      _stLock(); _State snap=_st; _stUnlock();
      const APIOTADispTheme& T=_THEMES[snap.theme];
      _tftLock();
      if (got) {
        if (ev.refreshAll) { _drawBase(snap.theme); _rAll(snap); }
        else { _rBadge(snap,T); _rProgress(snap,T); _rLog(snap); if (ev.refreshStats) _rStats(snap,T); }
      }
      TickType_t now=xTaskGetTickCount();
      if ((now-lastStat)>=pdMS_TO_TICKS(10000)) { lastStat=now; _rHeader(snap,T); _rStats(snap,T); }
      _tftUnlock(); vTaskDelay(1);
    }
  }
  void _ledRun() {
    bool s=false;
    for (;;) {
      uint32_t ms; int mode;
      _stLock(); ms=_blinkMs; mode=_ledMode; _stUnlock();
      if (mode==1)      digitalWrite(APIOTADISP_LED,HIGH);
      else if (mode==2) digitalWrite(APIOTADISP_LED,LOW);
      else { s=!s; digitalWrite(APIOTADISP_LED,s); }
      vTaskDelay(pdMS_TO_TICKS(ms));
    }
  }
  void _btnRun() {
    bool p1=HIGH, p2=HIGH; uint32_t b1down=0; bool b1held=false, b1long=false;
    for (;;) {
      bool b1=digitalRead(APIOTADISP_BTN1), b2=digitalRead(APIOTADISP_BTN2);
      uint32_t now=millis();
      // BTN1: short=theme cycle, hold 3s=WiFi portal
      if (b1==LOW && p1==HIGH) { b1down=now; b1held=true; b1long=false; }
      if (b1==LOW && b1held && !b1long && now-b1down>=3000) {
        b1long=true; _log("WiFi portal...");
        WiFiManager wm; wm.setConfigPortalTimeout(180); wm.startConfigPortal("APIOTA-ESP32-SETUP");
      }
      if (b1==HIGH && p1==LOW && b1held) { b1held=false; if (!b1long) { _stLock(); uint8_t t=(_st.theme+1)%3; _stUnlock(); _setTheme(t); } }
      // BTN2: force OTA check
      if (b2==HIGH && p2==LOW) { if (_ota) _ota->forceCheck(); _log("force OTA check"); }
      p1=b1; p2=b2; vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
};

// ── Theme palette (Dark / Light / Blue HUD) ────────────────────────
inline const APIOTADispTheme APIOTADisplay::_THEMES[3] = {
  { APIOTADISP_RGB(5,10,14),    APIOTADISP_RGB(13,24,32),   APIOTADISP_RGB(26,46,64),
    APIOTADISP_RGB(226,234,248),APIOTADISP_RGB(74,96,112),
    APIOTADISP_RGB(0,229,176),  APIOTADISP_RGB(59,130,246), APIOTADISP_RGB(245,158,11),
    APIOTADISP_RGB(34,197,94),  APIOTADISP_RGB(239,68,68),  APIOTADISP_RGB(139,92,246) },
  { APIOTADISP_RGB(240,244,248),APIOTADISP_RGB(255,255,255),APIOTADISP_RGB(200,214,224),
    APIOTADISP_RGB(26,42,56),   APIOTADISP_RGB(106,128,144),
    APIOTADISP_RGB(0,153,119),  APIOTADISP_RGB(29,95,184),  APIOTADISP_RGB(192,112,0),
    APIOTADISP_RGB(22,128,60),  APIOTADISP_RGB(185,28,28),  APIOTADISP_RGB(109,40,217) },
  { APIOTADISP_RGB(0,24,40),    APIOTADISP_RGB(0,40,64),    APIOTADISP_RGB(0,96,160),
    APIOTADISP_RGB(160,224,255),APIOTADISP_RGB(48,128,160),
    APIOTADISP_RGB(0,229,176),  APIOTADISP_RGB(96,192,255), APIOTADISP_RGB(255,208,96),
    APIOTADISP_RGB(0,255,136),  APIOTADISP_RGB(255,68,68),  APIOTADISP_RGB(192,128,255) }
};
