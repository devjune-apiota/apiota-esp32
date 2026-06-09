// ================================================================
//  apiota_wifi.h — Multi-WiFi NVS, MRU ordering, portal
//  ถูก include ใน main .ino หลังจากประกาศ global variables แล้ว
// ================================================================
#pragma once

#define NVS_NAMESPACE "apiota-wifi"

struct WifiCred {
  char ssid[33];
  char pass[65];
};

static WifiCred  g_wifiList[MAX_WIFI_SLOTS];
static uint8_t   g_wifiCount   = 0;
static uint8_t   g_wifiCurrent = 0;
static volatile bool g_portalActive = false;

// ── JSON helper (ใช้ใน OTA functions ด้วย) ───────────────────────
static String jget(const String& js, const char* key) {
  String sk = String('"') + key + "\":";
  int s = js.indexOf(sk);
  if (s < 0) return "";
  s += sk.length();
  while (s < (int)js.length() && (js[s]==' '||js[s]=='\n'||js[s]=='\r'||js[s]=='\t')) s++;
  if (s >= (int)js.length()) return "";
  char c = js[s];
  if (c == '"') {
    int e = s + 1;
    while (e < (int)js.length()) {
      if (js[e]=='\\') { e+=2; continue; }
      if (js[e]=='"') break;
      e++;
    }
    return js.substring(s+1,e);
  }
  if (c=='{' || c=='[') {
    char open=c, close=(c=='{')?'}':']';
    int depth=1, e=s+1;
    while (e<(int)js.length()&&depth>0) {
      if (js[e]==open) depth++; else if (js[e]==close) depth--; e++;
    }
    return js.substring(s,e);
  }
  int e=s;
  while (e<(int)js.length()&&js[e]!=','&&js[e]!='}'&&js[e]!=']'&&js[e]!='\n'&&js[e]!='\r') e++;
  String v=js.substring(s,e); v.trim(); return v;
}

// ── NVS Load / Save ─────────────────────────────────────────────
static void wifiLoadAll() {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) { g_wifiCount=0; return; }
  g_wifiCount = prefs.getUChar("count",0);
  if (g_wifiCount>MAX_WIFI_SLOTS) g_wifiCount=MAX_WIFI_SLOTS;
  for (uint8_t i=0;i<g_wifiCount;i++) {
    char ks[8],kp[8]; snprintf(ks,sizeof(ks),"s%d",i); snprintf(kp,sizeof(kp),"p%d",i);
    String s=prefs.getString(ks,""), p=prefs.getString(kp,"");
    strncpy(g_wifiList[i].ssid,s.c_str(),32); g_wifiList[i].ssid[32]='\0';
    strncpy(g_wifiList[i].pass,p.c_str(),64); g_wifiList[i].pass[64]='\0';
  }
  prefs.end();
  Serial.printf("[WIFI] loaded %d networks from NVS\n",g_wifiCount);
}

static void wifiSaveAll() {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE,false)) return;
  prefs.putUChar("count",g_wifiCount);
  for (uint8_t i=0;i<MAX_WIFI_SLOTS;i++) {
    char ks[8],kp[8]; snprintf(ks,sizeof(ks),"s%d",i); snprintf(kp,sizeof(kp),"p%d",i);
    if (i<g_wifiCount) { prefs.putString(ks,g_wifiList[i].ssid); prefs.putString(kp,g_wifiList[i].pass); }
    else               { prefs.remove(ks); prefs.remove(kp); }
  }
  prefs.end();
}

// ── CRUD ─────────────────────────────────────────────────────────
static void wifiAdd(const char* ssid, const char* pass) {
  if (!ssid||!ssid[0]) return;
  for (uint8_t i=0;i<g_wifiCount;i++) {
    if (strcmp(g_wifiList[i].ssid,ssid)==0) {
      strncpy(g_wifiList[i].pass,pass,64); g_wifiList[i].pass[64]='\0';
      wifiSaveAll(); return;
    }
  }
  if (g_wifiCount>=MAX_WIFI_SLOTS) {
    for (uint8_t i=0;i<MAX_WIFI_SLOTS-1;i++) g_wifiList[i]=g_wifiList[i+1];
    g_wifiCount=MAX_WIFI_SLOTS-1;
  }
  strncpy(g_wifiList[g_wifiCount].ssid,ssid,32); g_wifiList[g_wifiCount].ssid[32]='\0';
  strncpy(g_wifiList[g_wifiCount].pass,pass,64); g_wifiList[g_wifiCount].pass[64]='\0';
  g_wifiCount++; wifiSaveAll();
  Serial.printf("[WIFI] added: %s (total %d)\n",ssid,g_wifiCount);
}

__attribute__((unused)) static void wifiDelete(uint8_t index) {  // helper CRUD (ยังไม่มีที่เรียก — กัน -Wunused)
  if (index>=g_wifiCount) return;
  for (uint8_t i=index;i<g_wifiCount-1;i++) g_wifiList[i]=g_wifiList[i+1];
  g_wifiCount--; memset(&g_wifiList[g_wifiCount],0,sizeof(WifiCred));
  wifiSaveAll();
}

// ── MRU: เลื่อน WiFi ที่เพิ่ง connect ขึ้น slot 0 ──────────────
static void wifiMoveToFront(uint8_t idx) {
  if (idx==0||idx>=g_wifiCount) return;
  WifiCred recent=g_wifiList[idx];
  for (uint8_t i=idx;i>0;i--) g_wifiList[i]=g_wifiList[i-1];
  g_wifiList[0]=recent; g_wifiCurrent=0;
  wifiSaveAll();
  Serial.printf("[WIFI] MRU: '%s' → slot 0\n",g_wifiList[0].ssid);
}

static void wifiClearAll() {
  g_wifiCount=0; memset(g_wifiList,0,sizeof(g_wifiList));
  Preferences prefs; prefs.begin(NVS_NAMESPACE,false); prefs.clear(); prefs.end();
  WiFi.disconnect(true,true);
  // ล้าง device secret ด้วย
  Preferences ap; ap.begin("apiota",false); ap.remove("dev_sec"); ap.remove("dev_id"); ap.end();
}

// ── Connect ──────────────────────────────────────────────────────
static bool wifiTryConnect(uint8_t startIdx) {
  if (g_wifiCount==0) return false;
  WiFi.mode(WIFI_STA);
  for (uint8_t attempt=0;attempt<g_wifiCount;attempt++) {
    uint8_t idx=(startIdx+attempt)%g_wifiCount;
    if (!g_wifiList[idx].ssid[0]) continue;
    char m[48]; snprintf(m,sizeof(m),"trying WiFi: %s",g_wifiList[idx].ssid);
    pushLog(m);
    WiFi.disconnect(); DLY(100);
    WiFi.begin(g_wifiList[idx].ssid,g_wifiList[idx].pass);
    uint32_t t0=millis();
    while (WiFi.status()!=WL_CONNECTED&&(millis()-t0)<WIFI_CONNECT_TIMEOUT_MS) DLY(250);
    if (WiFi.status()==WL_CONNECTED) {
      char connSSID[33]; strncpy(connSSID,g_wifiList[idx].ssid,32); connSSID[32]='\0';
      if (idx>0) wifiMoveToFront(idx); else g_wifiCurrent=0;
      char ok[56]; snprintf(ok,sizeof(ok),"WiFi OK: %s (#1)",connSSID);
      pushLog(ok); return true;
    }
  }
  pushLog("all saved WiFi failed"); return false;
}

// ── WiFiManager portal ───────────────────────────────────────────
static bool wifiStartPortal(uint16_t timeoutSec) {
  g_portalActive=true;
  if (g_netTask) vTaskSuspend(g_netTask);
  pushLog("WiFi Manager portal opening...");

  ST_LOCK(); uint8_t ti=g_st.theme; ST_UNLOCK();
  const Theme& T=THEMES[ti];
  TFT_LOCK();
  tft.fillRect(0,30,SCR_W,108,T.bg);
  tft.setTextDatum(MC_DATUM); tft.setTextSize(1);
  tft.setTextColor(T.accent,T.bg); tft.drawString("WiFi Setup Portal Active",SCR_W/2,38);
  tft.setTextColor(T.text,T.bg);
  tft.drawString("Connect to: APIOTA-ESP32-SETUP",SCR_W/2,52);
  tft.drawString("then open 192.168.4.1",SCR_W/2,64);
  tft.setTextColor(T.muted,T.bg); tft.setTextDatum(TL_DATUM);
  char hdr[32]; snprintf(hdr,sizeof(hdr),"Saved WiFi (%d/%d):",g_wifiCount,MAX_WIFI_SLOTS);
  tft.drawString(hdr,6,80);
  for (uint8_t i=0;i<g_wifiCount&&i<4;i++) {
    char line[42]; snprintf(line,sizeof(line),"%d. %s%s",i+1,g_wifiList[i].ssid,(i==g_wifiCurrent)?" *":"");
    tft.setTextColor((i==g_wifiCurrent)?T.green:T.muted,T.bg);
    tft.drawString(line,12,92+i*10);
  }
  TFT_UNLOCK();

  WiFiManager wm;
  wm.setConfigPortalTimeout(timeoutSec); wm.setConnectTimeout(15);
  bool result=wm.startConfigPortal("APIOTA-ESP32-SETUP");

  if (result&&WiFi.status()==WL_CONNECTED) {
    String newSSID=wm.getWiFiSSID(), newPass=wm.getWiFiPass();
    if (newSSID.length()==0) newSSID=WiFi.SSID();
    if (newPass.length()==0) newPass=WiFi.psk();
    if (newSSID.length()==0||newPass.length()==0) {
      delay(300);
      if (newSSID.length()==0) newSSID=WiFi.SSID();
      if (newPass.length()==0) newPass=WiFi.psk();
    }
    if (newSSID.length()>0) { wifiAdd(newSSID.c_str(),newPass.c_str()); }
    else pushLog("WARN: WiFi SSID empty, not saved");
  }
  g_portalActive=false;
  if (g_netTask) vTaskResume(g_netTask);
  TFT_LOCK(); drawBase(ti); ST_LOCK(); SharedState snap=g_st; ST_UNLOCK(); rAll(snap,ti); TFT_UNLOCK();
  return result;
}
