// ================================================================
//  apiota_ui.h — TFT drawing helpers, themes, pushLog, sendEv
//  included by the main .ino after the global variables are declared
// ================================================================
#pragma once

// ── THEMES ──────────────────────────────────────────────────────
static const Theme THEMES[3] = {
  // Dark
  { RGB565(5,10,14),    RGB565(13,24,32),   RGB565(26,46,64),
    RGB565(226,234,248),RGB565(74,96,112),
    RGB565(0,229,176),  RGB565(59,130,246), RGB565(245,158,11),
    RGB565(34,197,94),  RGB565(239,68,68),  RGB565(139,92,246) },
  // Light
  { RGB565(240,244,248),RGB565(255,255,255),RGB565(200,214,224),
    RGB565(26,42,56),   RGB565(106,128,144),
    RGB565(0,153,119),  RGB565(29,95,184),  RGB565(192,112,0),
    RGB565(22,128,60),  RGB565(185,28,28),  RGB565(109,40,217) },
  // Blue HUD
  { RGB565(0,24,40),    RGB565(0,40,64),    RGB565(0,96,160),
    RGB565(160,224,255),RGB565(48,128,160),
    RGB565(0,229,176),  RGB565(96,192,255), RGB565(255,208,96),
    RGB565(0,255,136),  RGB565(255,68,68),  RGB565(192,128,255) }
};

// ── TFT PRIMITIVES ───────────────────────────────────────────────
static inline uint16_t stColor(OTAStatus st, const Theme& T) {
  switch (st) {
    case OTA_IDLE:        return T.muted;
    case OTA_CHECKING:    return T.blue;
    case OTA_DOWNLOADING: return T.amber;
    case OTA_VERIFYING:   return T.purple;
    case OTA_FLASHING:    return T.amber;
    case OTA_SUCCESS:     return T.green;
    case OTA_FAILED:      return T.red;
    case OTA_ROLLBACK:    return T.amber;
    default:              return T.muted;
  }
}
static inline const char* stLabel(OTAStatus st) {
  static const char* L[] = {"IDLE","CHECKING","DOWNLOAD","VERIFYING",
                             "FLASHING","SUCCESS","FAILED","ROLLBACK"};
  return L[(int)st < 8 ? (int)st : 0];
}
static void tftPBar(int x,int y,int w,int h,int pct,uint16_t fc,uint16_t bc,uint16_t boc){
  tft.fillRoundRect(x,y,w,h,3,bc);
  if(pct>0) tft.fillRoundRect(x,y,max(1,min(w,w*pct/100)),h,3,fc);
  tft.drawRoundRect(x,y,w,h,3,boc);
}
static void tftCard(int x,int y,int w,int h,const char* lbl,const char* val,uint16_t vc,const Theme& T){
  tft.fillRoundRect(x,y,w,h,3,T.card); tft.drawRoundRect(x,y,w,h,3,T.border);
  tft.setTextDatum(TC_DATUM); tft.setTextSize(1);
  tft.setTextColor(T.muted,T.card); tft.drawString(lbl,x+w/2,y+5);
  tft.setTextColor(vc,T.card); tft.drawString(val,x+w/2,y+17);
}
static void tftWifi(int x,int y,int rssi,const Theme& T){
  int b=(rssi>-50)?4:(rssi>-60)?3:(rssi>-70)?2:1;
  for(int i=0;i<4;i++){int bh=(i+1)*3; tft.fillRect(x+i*5,y+12-bh,4,bh,(i<b)?T.accent:T.muted);}
}
static void postDisplayEvent(const DisplayEvent& ev){
  if(xQueueSend(g_dispQ,&ev,0)!=pdTRUE){
    DisplayEvent dump; xQueueReceive(g_dispQ,&dump,0); xQueueSend(g_dispQ,&ev,0);
  }
}

// ── DRAW ZONES ───────────────────────────────────────────────────
static void drawBase(uint8_t ti) {
  const Theme& T = THEMES[ti];
  tft.fillScreen(T.bg);
  tft.fillRect(0,0,SCR_W,28,T.card);
  tft.drawFastHLine(0,27,SCR_W,T.border);
  tft.fillRoundRect(5,5,50,16,3,T.accent);
  tft.setTextDatum(ML_DATUM); tft.setTextSize(1);
  tft.setTextColor(TFT_BLACK,T.accent); tft.drawString("APIOTA",10,13);
  tft.setTextColor(T.text,T.card);
  String did = DEVICE_ID; if (did.length() > 14) did = did.substring(0,14);
  tft.drawString(did.c_str(),62,13);
  tft.drawFastHLine(0,138,SCR_W,T.border);
}

static void rHeader(const SharedState& s,const Theme& T) {
  tft.fillRect(SCR_W-126,0,126,28,T.card);
  tftWifi(SCR_W-122,7,s.wifiRssi,T);
  char b[16]; snprintf(b,sizeof(b),"%ddBm",s.wifiRssi);
  tft.setTextDatum(TL_DATUM); tft.setTextColor(T.muted,T.card); tft.setTextSize(1);
  tft.drawString(b, SCR_W-94, 3);
  String ip = WiFi.localIP().toString(); tft.drawString(ip.c_str(), SCR_W-94, 14);
}

static void rBadge(const SharedState& s,const Theme& T) {
  uint16_t sc = stColor(s.otaStatus,T);
  int bx=SCR_W-74, by=32, bw=68, bh=16;
  tft.fillRect(bx-2,by-2,bw+4,bh+4,T.bg);
  tft.drawRoundRect(bx,by,bw,bh,3,sc);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(sc,T.bg); tft.setTextSize(1);
  tft.drawString(stLabel(s.otaStatus),bx+bw/2,by+bh/2);
  tft.fillRect(6,30,SCR_W-82,28,T.bg);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(T.muted,T.bg);
  tft.drawString("OTA UPDATE",6,37);
  char vb[36];
  if (s.otaNewVer[0]) snprintf(vb,sizeof(vb),"v%s -> v%s",CURRENT_VERSION,s.otaNewVer);
  else                snprintf(vb,sizeof(vb),"v%s",CURRENT_VERSION);
  tft.setTextColor(T.text,T.bg); tft.drawString(vb,6,49);
}

static void rProgress(const SharedState& s,const Theme& T) {
  uint16_t pc = stColor(s.otaStatus,T);
  tftPBar(6,60,SCR_W-96,10,s.otaProgress,pc,T.card,T.border);
  tft.fillRect(SCR_W-82,56,76,16,T.bg);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(pc,T.bg);
  char b[8]; snprintf(b,sizeof(b),"%d%%",s.otaProgress);
  tft.drawString(b,SCR_W-76,66);
}

static void rStats(const SharedState& s,const Theme& T) {
  char hb[10],ub[12];
  snprintf(hb,sizeof(hb),"%dKB",(int)s.freeHeapKB);
  if (s.uptimeMin>=60) snprintf(ub,sizeof(ub),"%dh%02dm",(int)(s.uptimeMin/60),(int)(s.uptimeMin%60));
  else                 snprintf(ub,sizeof(ub),"%dm",(int)s.uptimeMin);
  char vshort[10]; snprintf(vshort,sizeof(vshort),"v%s",CURRENT_VERSION);
  char rssiBuf[10]; snprintf(rssiBuf,sizeof(rssiBuf),"%ddB",(int)s.wifiRssi);
  tftCard(6,   78,74,36,"HEAP",  hb,    (s.freeHeapKB>100)?T.green:T.amber, T);
  tftCard(82,  78,74,36,"UPTIME",ub,    T.blue, T);
  tftCard(158, 78,74,36,"RSSI",  rssiBuf,(s.wifiRssi>-65)?T.green:(s.wifiRssi>-75?T.amber:T.red),T);
  tftCard(234, 78,80,36,"VER",   vshort, T.accent, T);
  tft.fillRect(6,118,SCR_W-12,14,T.bg);
  tft.setTextDatum(TL_DATUM); tft.setTextColor(T.muted,T.bg); tft.setTextSize(1);
  tft.drawString(("IP: "+WiFi.localIP().toString()).c_str(),8,120);
}

static void rLog(const SharedState& s,const Theme& T) {
  uint16_t lc = stColor(s.otaStatus,T);
  tft.fillRect(0,140,SCR_W,SCR_H-140,T.card);
  uint16_t cols[3]={T.muted,T.muted,lc};
  int ys[3]={143,152,161};
  for (int i=0;i<3;i++) {
    if (!s.logLines[i][0]) continue;
    tft.setTextDatum(TL_DATUM); tft.setTextColor(cols[i],T.card); tft.setTextSize(1);
    tft.drawString(s.logLines[i],4,ys[i]);
  }
}

static void rAll(const SharedState& s,uint8_t ti) {
  const Theme& T=THEMES[ti];
  rHeader(s,T); rBadge(s,T); rProgress(s,T); rStats(s,T); rLog(s,T);
}

// ── PUSH LOG & SEND EVENT ────────────────────────────────────────
static void pushLog(const char* msg) {
  ST_LOCK();
  strncpy(g_st.logLines[0],g_st.logLines[1],63);
  strncpy(g_st.logLines[1],g_st.logLines[2],63);
  strncpy(g_st.logLines[2],msg,63); g_st.logLines[2][63]='\0';
  SharedState snap=g_st; uint8_t ti=g_st.theme;
  ST_UNLOCK();
  Serial.printf("[LOG] %s\n",msg);
  if (!g_tasksStarted) rLog(snap,THEMES[ti]);  // boot mode: render directly to the screen
}

static void sendEv(OTAStatus st,int pct,const char* msg,
                   const char* nv,bool rs,bool ra) {
  ST_LOCK();
  g_st.otaStatus=st;
  if (pct>=0) g_st.otaProgress=pct;
  if (nv&&nv[0]){ strncpy(g_st.otaNewVer,nv,15); g_st.otaNewVer[15]='\0'; }
  strncpy(g_st.logLines[0],g_st.logLines[1],63);
  strncpy(g_st.logLines[1],g_st.logLines[2],63);
  strncpy(g_st.logLines[2],msg,63); g_st.logLines[2][63]='\0';
  ST_UNLOCK();
  DisplayEvent ev={};
  ev.status=st; ev.progress=pct; ev.refreshStats=rs; ev.refreshAll=ra;
  strncpy(ev.log,msg,63);
  if (nv){ strncpy(ev.newVer,nv,15); ev.newVer[15]='\0'; }
  postDisplayEvent(ev);
  Serial.printf("[OTA] %s\n",msg);
}

// ── BLOCKED SCREEN ───────────────────────────────────────────────
static void showBlockedScreen(const char* title,const char* line1,
                              const char* line2,const char* line3) {
  ST_LOCK(); uint8_t ti=g_st.theme; ST_UNLOCK();
  const Theme& T=THEMES[ti];
  TFT_LOCK();
  tft.fillRect(0,28,SCR_W,110,T.bg); tft.setTextDatum(MC_DATUM); tft.setTextSize(1);
  tft.drawRoundRect(4,32,SCR_W-8,102,4,T.red);
  tft.setTextColor(T.red,T.bg);   tft.drawString(title, SCR_W/2,48);
  tft.setTextColor(T.amber,T.bg); if(line1&&line1[0]) tft.drawString(line1,SCR_W/2,68);
  tft.setTextColor(T.text,T.bg);  if(line2&&line2[0]) tft.drawString(line2,SCR_W/2,86);
  tft.setTextColor(T.muted,T.bg); if(line3&&line3[0]) tft.drawString(line3,SCR_W/2,104);
  TFT_UNLOCK();
}
