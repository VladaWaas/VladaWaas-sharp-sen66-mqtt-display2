/*
 * ============================================================
 *  Sharp Memory LCD + SEN66 Sensor + MQTT (Home Assistant)
 *  ESP32-C3-MINI1 | PlatformIO + Arduino Framework
 * ============================================================
 *  Verze: 2.0.0
 *  Datum: 2026-02-13
 * 
 *  Piny:
 *    SPI (displej): CLK=6, MOSI=7, MISO=2, CS=3
 *    I2C (SEN66):   SDA=10, SCL=8
 * ============================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SharpMem.h>
#include <SensirionI2cSen66.h>
#include <ArduinoJson.h>
#include "config.h"

// =============================================
//  KONFIGURACE - UPRAVTE PODLE POTŘEBY
// =============================================

// SPI piny (Sharp LCD)
#define PIN_SPI_CLK   6
#define PIN_SPI_MOSI  7
#define PIN_SPI_MISO  2   // nepoužitý - displej je write-only
#define PIN_SPI_CS    3

// I2C piny (SEN66)
#define PIN_SDA  10
#define PIN_SCL  8

// Displej
#define DISPLAY_WIDTH  400
#define DISPLAY_HEIGHT 240
#define BLACK 0
#define WHITE 1

// Intervaly (ms)
#define SENSOR_READ_INTERVAL   2000   // čtení senzoru každé 2s
#define MQTT_RECONNECT_INTERVAL  5000

// =============================================
//  MQTT TOPICS
// =============================================

// Příchozí (subscribe)
#define TOPIC_TEXT       "sharp/display/text"
#define TOPIC_CLEAR      "sharp/display/clear"
#define TOPIC_COMMAND    "sharp/display/command"
#define TOPIC_BRIGHTNESS "sharp/display/brightness"

// Odchozí (publish)
#define TOPIC_STATUS     "sharp/status"
#define TOPIC_SENSOR     "sharp/sensor"     // JSON se všemi hodnotami
#define TOPIC_TEMP       "sharp/sensor/temperature"
#define TOPIC_HUMIDITY   "sharp/sensor/humidity"
#define TOPIC_PM1        "sharp/sensor/pm1"
#define TOPIC_PM25       "sharp/sensor/pm25"
#define TOPIC_PM4        "sharp/sensor/pm4"
#define TOPIC_PM10       "sharp/sensor/pm10"
#define TOPIC_VOC        "sharp/sensor/voc"
#define TOPIC_NOX        "sharp/sensor/nox"
#define TOPIC_CO2        "sharp/sensor/co2"

// =============================================
//  GLOBÁLNÍ OBJEKTY
// =============================================

Adafruit_SharpMem display(PIN_SPI_CLK, PIN_SPI_MOSI, PIN_SPI_CS, 
                           DISPLAY_WIDTH, DISPLAY_HEIGHT);
SensirionI2cSen66 sen66;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
WebServer webServer(80);

// =============================================
//  DATA SENZORU
// =============================================

struct SensorData {
  float pm1   = 0.0;
  float pm25  = 0.0;
  float pm4   = 0.0;
  float pm10  = 0.0;
  float temperature = 0.0;
  float humidity    = 0.0;
  float voc   = 0.0;
  float nox   = 0.0;
  uint16_t co2 = 0;
  bool valid = false;
} sensorData;

// =============================================
//  STAV APLIKACE
// =============================================

unsigned long lastSensorRead = 0;
unsigned long lastMqttPublish = 0;
unsigned long lastDisplayRefresh = 0;
unsigned long lastMqttReconnect = 0;
unsigned long lastTmepRequest = 0;
unsigned long firstValidSensorAt = 0;

AppConfig appConfig;

bool sen66Ready = false;
bool displayOverride = false;       // true = zobrazuje custom text z MQTT
unsigned long displayOverrideUntil = 0; // kdy přepnout zpět na senzory

String overrideText = "";
int overrideTextSize = 2;
int overrideX = 10;
int overrideY = 10;

// =============================================
//  DISPLAY - POMOCNÉ FUNKCE
// =============================================


void applyDisplaySettings() {
  display.setRotation(appConfig.displayRotation % 4);
  if (appConfig.displayInvertRequested) {
    Serial.println("Display: Inverze je pozadovana, HW inverze neni na Sharp LCD podporovana.");
  }
}

bool sensorValuesLookValid(const float pm1, const float pm25, const float pm4, const float pm10,
                           const float hum, const float temp, const float voc, const float nox,
                           const uint16_t co2) {
  if (isnan(pm1) || isnan(pm25) || isnan(pm4) || isnan(pm10) || isnan(hum) || isnan(temp) ||
      isnan(voc) || isnan(nox)) return false;

  if (temp < -40.0f || temp > 85.0f) return false;
  if (hum < 0.0f || hum > 100.0f) return false;
  if (pm1 < 0.0f || pm1 > 1000.0f) return false;
  if (pm25 < 0.0f || pm25 > 1000.0f) return false;
  if (pm4 < 0.0f || pm4 > 1000.0f) return false;
  if (pm10 < 0.0f || pm10 > 1000.0f) return false;
  if (voc < 0.0f || voc > 500.0f) return false;
  if (nox < 0.0f || nox > 500.0f) return false;
  if (co2 < 350 || co2 > 10000) return false;

  return true;
}

void drawCenteredText(const char* text, int y, int textSize) {
  display.setTextSize(textSize);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((DISPLAY_WIDTH - w) / 2, y);
  display.print(text);
}

void drawRightAlignedText(const char* text, int y, int textSize) {
  display.setTextSize(textSize);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(DISPLAY_WIDTH - w - 5, y);
  display.print(text);
}

void drawDividerLine(int y) {
  display.drawLine(5, y, DISPLAY_WIDTH - 5, y, BLACK);
}

// Ikona teploměru (jednoduchý symbol)
void drawThermIcon(int x, int y) {
  display.drawCircle(x + 3, y + 12, 4, BLACK);
  display.drawRect(x + 1, y, 5, 12, BLACK);
  display.fillCircle(x + 3, y + 12, 3, BLACK);
}

// Ikona kapky (vlhkost)
void drawDropIcon(int x, int y) {
  display.drawPixel(x + 3, y, BLACK);
  display.drawLine(x + 2, y + 1, x + 4, y + 1, BLACK);
  display.drawLine(x + 1, y + 2, x + 5, y + 2, BLACK);
  display.drawLine(x, y + 3, x + 6, y + 3, BLACK);
  display.drawLine(x, y + 4, x + 6, y + 4, BLACK);
  display.drawLine(x, y + 5, x + 6, y + 5, BLACK);
  display.drawLine(x + 1, y + 6, x + 5, y + 6, BLACK);
  display.drawLine(x + 2, y + 7, x + 4, y + 7, BLACK);
}

// =============================================
//  DISPLAY - HLAVNÍ OBRAZOVKY
// =============================================

// Hodnocení kvality vzduchu podle PM2.5
const char* getAirQuality(float pm25) {
  if (pm25 < 12.0)  return "VYNIKAJICI";
  if (pm25 < 35.4)  return "DOBRE";
  if (pm25 < 55.4)  return "PRIJATELNE";
  if (pm25 < 150.4) return "SPATNE";
  if (pm25 < 250.4) return "VELMI SPATNE";
  return "NEBEZPECNE";
}

// Hlavní obrazovka se senzory
void drawSensorScreen() {
  display.clearDisplay();
  display.setTextColor(BLACK);
  
  char buf[64];
  
  // === STATUS BAR (y=0..22) ===
  display.setTextSize(1);
  
  // WiFi status
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(buf, sizeof(buf), "WiFi:%s", WiFi.localIP().toString().c_str());
  } else {
    snprintf(buf, sizeof(buf), "WiFi:---");
  }
  display.setCursor(5, 5);
  display.print(buf);
  
  // MQTT status
  display.setCursor(200, 5);
  display.print(mqtt.connected() ? "MQTT:OK" : "MQTT:---");
  
  // SEN66 status
  display.setCursor(290, 5);
  display.print(sen66Ready ? "SEN66:OK" : "SEN66:---");
  
  // Uptime
  unsigned long uptimeSec = millis() / 1000;
  unsigned long hrs = uptimeSec / 3600;
  unsigned long mins = (uptimeSec % 3600) / 60;
  snprintf(buf, sizeof(buf), "%luh%02lum", hrs, mins);
  drawRightAlignedText(buf, 5, 1);
  
  drawDividerLine(18);
  
  if (!sensorData.valid) {
    drawCenteredText("Cekam na data", 80, 2);
    drawCenteredText("ze senzoru SEN66...", 110, 2);
    display.refresh();
    return;
  }
  
  // === TEPLOTA & VLHKOST (y=24..80) ===
  // Teplota - velký font
  drawThermIcon(15, 28);
  snprintf(buf, sizeof(buf), "%.1f", sensorData.temperature);
  display.setTextSize(4);
  display.setCursor(35, 25);
  display.print(buf);
  // Stupně C menším fontem
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(buf, 35, 25, &x1, &y1, &w, &h);
  display.setTextSize(2);
  display.setCursor(35 + w + 5, 25);
  display.print("o");
  display.setCursor(35 + w + 5, 40);
  display.print("C");
  
  // Vlhkost - velký font
  drawDropIcon(220, 28);
  snprintf(buf, sizeof(buf), "%.1f", sensorData.humidity);
  display.setTextSize(4);
  display.setCursor(240, 25);
  display.print(buf);
  display.getTextBounds(buf, 240, 25, &x1, &y1, &w, &h);
  display.setTextSize(2);
  display.setCursor(240 + w + 5, 30);
  display.print("%");
  
  drawDividerLine(68);
  
  // === PM HODNOTY (y=72..140) ===
  display.setTextSize(1);
  // Záhlaví
  display.setCursor(15, 74);
  display.print("PM1.0");
  display.setCursor(115, 74);
  display.print("PM2.5");
  display.setCursor(215, 74);
  display.print("PM4.0");
  display.setCursor(315, 74);
  display.print("PM10");
  
  // Hodnoty - větší font
  display.setTextSize(3);
  snprintf(buf, sizeof(buf), "%.0f", sensorData.pm1);
  display.setCursor(10, 90);
  display.print(buf);
  
  snprintf(buf, sizeof(buf), "%.0f", sensorData.pm25);
  display.setCursor(110, 90);
  display.print(buf);
  
  snprintf(buf, sizeof(buf), "%.0f", sensorData.pm4);
  display.setCursor(210, 90);
  display.print(buf);
  
  snprintf(buf, sizeof(buf), "%.0f", sensorData.pm10);
  display.setCursor(310, 90);
  display.print(buf);
  
  // Jednotky
  display.setTextSize(1);
  display.setCursor(15, 118);
  display.print("ug/m3");
  display.setCursor(115, 118);
  display.print("ug/m3");
  display.setCursor(215, 118);
  display.print("ug/m3");
  display.setCursor(315, 118);
  display.print("ug/m3");
  
  drawDividerLine(132);
  
  // === VOC, NOx, CO2 (y=136..200) ===
  display.setTextSize(1);
  display.setCursor(15, 138);
  display.print("VOC Index");
  display.setCursor(155, 138);
  display.print("NOx Index");
  display.setCursor(295, 138);
  display.print("CO2");
  
  display.setTextSize(3);
  snprintf(buf, sizeof(buf), "%.0f", sensorData.voc);
  display.setCursor(15, 152);
  display.print(buf);
  
  snprintf(buf, sizeof(buf), "%.0f", sensorData.nox);
  display.setCursor(155, 152);
  display.print(buf);
  
  snprintf(buf, sizeof(buf), "%u", sensorData.co2);
  display.setCursor(280, 152);
  display.print(buf);
  
  display.setTextSize(1);
  display.setCursor(350, 170);
  display.print("ppm");
  
  drawDividerLine(185);
  
  // === AIR QUALITY BAR (y=190..235) ===
  const char* quality = getAirQuality(sensorData.pm25);
  display.setTextSize(1);
  display.setCursor(15, 192);
  display.print("Kvalita vzduchu:");
  
  display.setTextSize(3);
  display.setCursor(15, 208);
  display.print(quality);
  
  // Indikátor bar
  float barValue = min(sensorData.pm25 / 150.0f, 1.0f);
  int barWidth = (int)(barValue * 120);
  display.drawRect(270, 200, 122, 24, BLACK);
  display.fillRect(271, 201, barWidth, 22, BLACK);
  
  display.refresh();
}

// Obrazovka s custom textem (z MQTT)
void drawCustomTextScreen() {
  display.clearDisplay();
  display.setTextColor(BLACK);
  display.setTextSize(overrideTextSize);
  display.setCursor(overrideX, overrideY);
  display.println(overrideText);
  display.refresh();
}

// Boot/splash screen
void drawSplashScreen() {
  display.clearDisplay();
  display.setTextColor(BLACK);
  
  drawCenteredText("Sharp LCD + SEN66", 40, 3);
  drawCenteredText("MQTT Dashboard", 80, 2);
  
  drawDividerLine(110);
  
  display.setTextSize(1);
  display.setCursor(30, 125);
  display.print("WiFi: ");
  display.print(appConfig.wifiSsid);
  
  display.setCursor(30, 140);
  display.print("MQTT: ");
  display.print(appConfig.mqttServer);
  
  display.setCursor(30, 160);
  display.print("Inicializace...");
  
  display.refresh();
}

// =============================================
//  SEN66 SENZOR
// =============================================

#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0

void initSEN66() {
  Serial.println("SEN66: Inicializace I2C...");
  Wire.begin(PIN_SDA, PIN_SCL);
  
  sen66.begin(Wire, SEN66_I2C_ADDR_6B);
  
  int16_t error = sen66.deviceReset();
  if (error != NO_ERROR) {
    char msg[64];
    errorToString(error, msg, sizeof(msg));
    Serial.printf("SEN66: deviceReset() CHYBA: %s\n", msg);
    sen66Ready = false;
    return;
  }
  
  delay(1200); // SEN66 potřebuje čas po resetu
  
  // Přečíst sériové číslo
  int8_t serialNumber[32] = {0};
  error = sen66.getSerialNumber(serialNumber, 32);
  if (error != NO_ERROR) {
    char msg[64];
    errorToString(error, msg, sizeof(msg));
    Serial.printf("SEN66: getSerialNumber() CHYBA: %s\n", msg);
  } else {
    Serial.printf("SEN66: S/N: %s\n", (const char*)serialNumber);
  }
  
  // Spustit měření
  error = sen66.startContinuousMeasurement();
  if (error != NO_ERROR) {
    char msg[64];
    errorToString(error, msg, sizeof(msg));
    Serial.printf("SEN66: startContinuousMeasurement() CHYBA: %s\n", msg);
    sen66Ready = false;
    return;
  }
  
  sen66Ready = true;
  Serial.println("SEN66: OK, mereni spusteno!");
}

void readSEN66() {
  if (!sen66Ready) return;
  
  float pm1, pm25, pm4, pm10, hum, temp, voc, nox;
  uint16_t co2;
  
  int16_t error = sen66.readMeasuredValues(
    pm1, pm25, pm4, pm10, hum, temp, voc, nox, co2
  );
  
  if (error != NO_ERROR) {
    char msg[64];
    errorToString(error, msg, sizeof(msg));
    Serial.printf("SEN66: readMeasuredValues() CHYBA: %s\n", msg);
    return;
  }
  
  // Kontrola platnosti (SEN66 vrací NaN/0xFFFF při inicializaci)
  if (!sensorValuesLookValid(pm1, pm25, pm4, pm10, hum, temp, voc, nox, co2)) {
    Serial.println("SEN66: namerena neplatna data, preskakuji");
    return;
  }
  
  sensorData.pm1  = pm1;
  sensorData.pm25 = pm25;
  sensorData.pm4  = pm4;
  sensorData.pm10 = pm10;
  sensorData.temperature = temp;
  sensorData.humidity    = hum;
  sensorData.voc  = voc;
  sensorData.nox  = nox;
  sensorData.co2  = co2;
  sensorData.valid = true;
  if (firstValidSensorAt == 0) firstValidSensorAt = millis();
  
  Serial.printf("SEN66: T=%.1f H=%.1f PM2.5=%.1f VOC=%.0f NOx=%.0f CO2=%u\n",
    temp, hum, pm25, voc, nox, co2);
}


String formatFloat1(const float value) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f", value);
  return String(buf);
}

String formatFloat0(const float value) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%.0f", value);
  return String(buf);
}

void replaceAllTokens(String& target, const String& token, const String& value) {
  target.replace("*" + token + "*", value);
  target.replace("{" + token + "}", value);
}

String buildTmepQueryParams() {
  String params = appConfig.tmepParams;

  replaceAllTokens(params, "TEMP", formatFloat1(sensorData.temperature));
  replaceAllTokens(params, "HUM", formatFloat1(sensorData.humidity));
  replaceAllTokens(params, "PM1", formatFloat1(sensorData.pm1));
  replaceAllTokens(params, "PM2", formatFloat1(sensorData.pm25));
  replaceAllTokens(params, "PM4", formatFloat1(sensorData.pm4));
  replaceAllTokens(params, "PM10", formatFloat1(sensorData.pm10));
  replaceAllTokens(params, "VOC", formatFloat0(sensorData.voc));
  replaceAllTokens(params, "NOX", formatFloat0(sensorData.nox));
  replaceAllTokens(params, "CO2", String(sensorData.co2));

  return params;
}

String buildTmepRequestUrl() {
  if (appConfig.tmepDomain.length() == 0 || appConfig.tmepParams.length() == 0 || !sensorData.valid) return "";
  return "http://" + appConfig.tmepDomain + ".tmep.cz/?" + buildTmepQueryParams();
}

bool sendTmepRequest(const bool manualTrigger) {
  if (appConfig.tmepDomain.length() == 0 || appConfig.tmepParams.length() == 0) {
    Serial.println("TMEP: domena nebo parametry nejsou nastaveny, request preskocen");
    return false;
  }
  if (!sensorData.valid) {
    Serial.println("TMEP: nejsou validni data senzoru, request preskocen");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("TMEP: WiFi neni pripojena, request preskocen");
    return false;
  }

  String url = buildTmepRequestUrl();
  if (url.length() == 0) return false;

  HTTPClient http;
  http.setTimeout(5000);
  if (!http.begin(url)) {
    Serial.println("TMEP: Nelze inicializovat HTTP request");
    return false;
  }

  int httpCode = http.GET();
  String response = http.getString();
  http.end();

  if (httpCode > 0 && httpCode < 400) {
    Serial.printf("TMEP: %srequest OK, HTTP %d, URL: %s\n", manualTrigger ? "manual " : "", httpCode, url.c_str());
    return true;
  }

  Serial.printf("TMEP: %srequest CHYBA, HTTP %d, URL: %s, body: %s\n",
    manualTrigger ? "manual " : "", httpCode, url.c_str(), response.c_str());
  return false;
}

void handleWebRoot() {
  const char* html = R"HTML(<!doctype html><html lang="cs"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>SEN66 panel</title><style>body{font-family:Arial,sans-serif;margin:0;background:#f3f5f7;color:#222}header{background:#0f172a;color:#fff;padding:12px 16px}main{padding:16px;max-width:980px;margin:0 auto}.tabs{display:flex;gap:8px;margin-bottom:12px}.tab{padding:10px 14px;border:0;border-radius:8px;background:#dbe2ea;cursor:pointer}.tab.active{background:#2563eb;color:#fff}.panel{display:none;background:#fff;padding:16px;border-radius:10px;box-shadow:0 1px 3px rgba(0,0,0,.15)}.panel.active{display:block}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:10px}.card{border:1px solid #e5e7eb;border-radius:8px;padding:10px}label{display:block;font-size:.9rem;margin-top:8px}input{width:100%;padding:8px;border:1px solid #cbd5e1;border-radius:6px}button.save,button.secondary{margin-top:12px;padding:10px 14px;color:#fff;border:0;border-radius:8px;cursor:pointer}button.save{background:#16a34a}button.secondary{background:#2563eb}.muted{color:#666;font-size:.85rem}.ok{color:#166534}.err{color:#b91c1c}code.url{display:block;padding:8px;background:#f1f5f9;border-radius:6px;word-break:break-all}</style></head><body><header><h2>SEN66 MQTT displej</h2></header><main><div class="tabs"><button class="tab active" data-tab="data">Aktuální data</button><button class="tab" data-tab="cfg">Konfigurace</button></div><section id="data" class="panel active"><div class="grid" id="cards"></div><p class="muted" id="status"></p></section><section id="cfg" class="panel"><form id="cfgForm"><h3>WiFi</h3><label>SSID<input name="wifiSsid" required></label><label>Heslo<input type="password" name="wifiPassword"></label><h3>MQTT</h3><label>Server<input name="mqttServer" required></label><label>Port<input type="number" min="1" max="65535" name="mqttPort" required></label><label>Uživatel<input name="mqttUser"></label><label>Heslo<input type="password" name="mqttPassword"></label><h3>TMEP.cz</h3><label>Doména pro zasílání hodnot<input name="tmepDomain" placeholder="xxk4sk-g6rxfh"></label><label>Parametry požadavku<input name="tmepParams" placeholder="tempV=*TEMP*&humV=*HUM*&co2=*CO2*"></label><p class="muted">Použitelné proměnné: *TEMP*, *HUM*, *PM1*, *PM2*, *PM4*, *PM10*, *VOC*, *NOX*, *CO2*.</p><p class="muted">Reálné URL volané na TMEP.cz:</p><code id="tmepUrl" class="url muted">Není dostupné</code><button id="tmepSendBtn" class="secondary" type="button">Odeslat TMEP request ručně</button><p id="tmepMsg" class="muted"></p><h3>Displej</h3><label>Rotace (0-3)<input type="number" min="0" max="3" name="displayRotation" required></label><label>Inverze (0/1)<input type="number" min="0" max="1" name="displayInvertRequested" required></label><h3>Intervaly (ms)</h3><label>Překreslení displeje<input type="number" min="500" name="displayRefreshInterval" required></label><label>MQTT publish<input type="number" min="1000" name="mqttPublishInterval" required></label><label>TMEP request interval<input type="number" min="1000" name="tmepRequestInterval" required></label><label>MQTT warmup delay<input type="number" min="1000" name="mqttWarmupDelay" required></label><button class="save" type="submit">Uložit konfiguraci</button><p id="cfgMsg" class="muted"></p></form></section></main><script>const tabs=document.querySelectorAll('.tab');tabs.forEach(t=>t.onclick=()=>{tabs.forEach(x=>x.classList.remove('active'));document.querySelectorAll('.panel').forEach(p=>p.classList.remove('active'));t.classList.add('active');document.getElementById(t.dataset.tab).classList.add('active')});async function loadData(){const r=await fetch('/api/data');const d=await r.json();const cards=document.getElementById('cards');cards.innerHTML='';for(const [k,v] of Object.entries(d.values)){const c=document.createElement('div');c.className='card';c.innerHTML=`<strong>${k}</strong><div>${v}</div>`;cards.appendChild(c)}document.getElementById('status').textContent=`WiFi: ${d.wifi} | MQTT: ${d.mqtt} | validní data: ${d.valid} | uptime: ${d.uptime}s`;const tmepUrlEl=document.getElementById('tmepUrl');tmepUrlEl.textContent=d.tmepUrl||'Není dostupné';tmepUrlEl.className=d.tmepUrl?'url':'url muted'}async function loadCfg(){const r=await fetch('/api/config');const c=await r.json();const f=document.getElementById('cfgForm');Object.keys(c).forEach(k=>{if(f[k])f[k].value=c[k]})}document.getElementById('cfgForm').onsubmit=async(e)=>{e.preventDefault();const f=e.target;const payload=Object.fromEntries(new FormData(f).entries());const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});const txt=await r.text();const m=document.getElementById('cfgMsg');m.textContent=txt;m.className=r.ok?'ok':'err'};document.getElementById('tmepSendBtn').onclick=async()=>{const r=await fetch('/api/tmep/send',{method:'POST'});const txt=await r.text();const m=document.getElementById('tmepMsg');m.textContent=txt;m.className=r.ok?'ok':'err';await loadData()};loadData();loadCfg();setInterval(loadData,2000);</script></body></html>)HTML";
  webServer.send(200, "text/html; charset=utf-8", html);
}

void handleApiData() {
  JsonDocument doc;
  doc["wifi"] = WiFi.status() == WL_CONNECTED ? "connected" : "disconnected";
  doc["mqtt"] = mqtt.connected() ? "connected" : "disconnected";
  doc["valid"] = sensorData.valid;
  doc["uptime"] = millis() / 1000;
  doc["tmepUrl"] = buildTmepRequestUrl();

  JsonObject values = doc["values"].to<JsonObject>();
  values["temperature"] = round(sensorData.temperature * 10) / 10.0;
  values["humidity"] = round(sensorData.humidity * 10) / 10.0;
  values["pm1"] = round(sensorData.pm1 * 10) / 10.0;
  values["pm25"] = round(sensorData.pm25 * 10) / 10.0;
  values["pm4"] = round(sensorData.pm4 * 10) / 10.0;
  values["pm10"] = round(sensorData.pm10 * 10) / 10.0;
  values["voc"] = round(sensorData.voc);
  values["nox"] = round(sensorData.nox);
  values["co2"] = sensorData.co2;

  char payload[1024];
  serializeJson(doc, payload, sizeof(payload));
  webServer.send(200, "application/json", payload);
}

void handleApiConfigGet() {
  JsonDocument doc;
  doc["wifiSsid"] = appConfig.wifiSsid;
  doc["wifiPassword"] = appConfig.wifiPassword;
  doc["mqttServer"] = appConfig.mqttServer;
  doc["mqttPort"] = appConfig.mqttPort;
  doc["mqttUser"] = appConfig.mqttUser;
  doc["mqttPassword"] = appConfig.mqttPassword;
  doc["tmepDomain"] = appConfig.tmepDomain;
  doc["tmepParams"] = appConfig.tmepParams;
  doc["displayRotation"] = appConfig.displayRotation;
  doc["displayInvertRequested"] = appConfig.displayInvertRequested ? 1 : 0;
  doc["displayRefreshInterval"] = appConfig.displayRefreshInterval;
  doc["mqttPublishInterval"] = appConfig.mqttPublishInterval;
  doc["tmepRequestInterval"] = appConfig.tmepRequestInterval;
  doc["mqttWarmupDelay"] = appConfig.mqttWarmupDelay;

  char payload[1024];
  serializeJson(doc, payload, sizeof(payload));
  webServer.send(200, "application/json", payload);
}

void handleApiConfigPost() {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, webServer.arg("plain"));
  if (err) {
    webServer.send(400, "text/plain", "Neplatny JSON");
    return;
  }

  AppConfig updated = appConfig;
  if (doc["wifiSsid"].is<const char*>()) updated.wifiSsid = doc["wifiSsid"].as<String>();
  if (doc["wifiPassword"].is<const char*>()) updated.wifiPassword = doc["wifiPassword"].as<String>();
  if (doc["mqttServer"].is<const char*>()) updated.mqttServer = doc["mqttServer"].as<String>();
  if (doc["mqttUser"].is<const char*>()) updated.mqttUser = doc["mqttUser"].as<String>();
  if (doc["mqttPassword"].is<const char*>()) updated.mqttPassword = doc["mqttPassword"].as<String>();
  if (doc["mqttClientId"].is<const char*>()) updated.mqttClientId = doc["mqttClientId"].as<String>();
  if (doc["tmepDomain"].is<const char*>()) updated.tmepDomain = doc["tmepDomain"].as<String>();
  if (doc["tmepParams"].is<const char*>()) updated.tmepParams = doc["tmepParams"].as<String>();

  updated.mqttPort = doc["mqttPort"] | updated.mqttPort;
  updated.displayRotation = (uint8_t)(doc["displayRotation"] | updated.displayRotation);
  int newInvert = doc["displayInvertRequested"] | (updated.displayInvertRequested ? 1 : 0);
  updated.displayInvertRequested = (newInvert == 1);
  updated.displayRefreshInterval = doc["displayRefreshInterval"] | updated.displayRefreshInterval;
  updated.mqttPublishInterval = doc["mqttPublishInterval"] | updated.mqttPublishInterval;
  updated.tmepRequestInterval = doc["tmepRequestInterval"] | updated.tmepRequestInterval;
  updated.mqttWarmupDelay = doc["mqttWarmupDelay"] | updated.mqttWarmupDelay;

  if (!validateConfig(updated)) {
    webServer.send(400, "text/plain", "Neplatne hodnoty konfigurace");
    return;
  }

  if (!saveConfig(updated)) {
    webServer.send(500, "text/plain", "Nepodarilo se ulozit konfiguraci");
    return;
  }

  appConfig = updated;
  mqtt.setServer(appConfig.mqttServer.c_str(), appConfig.mqttPort);
  applyDisplaySettings();

  webServer.send(200, "text/plain", "Konfigurace ulozena, zarizeni se restartuje...");
  delay(300);
  ESP.restart();
}

void handleApiTmepSend() {
  bool ok = sendTmepRequest(true);
  if (ok) {
    webServer.send(200, "text/plain", "TMEP request byl uspesne odeslan");
    return;
  }
  webServer.send(500, "text/plain", "TMEP request se nepodarilo odeslat (zkontrolujte URL, WiFi a data)");
}

void setupWebServer() {
  webServer.on("/", HTTP_GET, handleWebRoot);
  webServer.on("/api/data", HTTP_GET, handleApiData);
  webServer.on("/api/config", HTTP_GET, handleApiConfigGet);
  webServer.on("/api/config", HTTP_POST, handleApiConfigPost);
  webServer.on("/api/tmep/send", HTTP_POST, handleApiTmepSend);
  webServer.begin();
  Serial.println("WEB: Server bezi na portu 80");
}

// =============================================
//  MQTT - CALLBACK
// =============================================

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.printf("MQTT RX [%s]: %s\n", topic, message.c_str());
  
  // --- TEXT: Zobraz text na displeji ---
  if (strcmp(topic, TOPIC_TEXT) == 0) {
    overrideText = message;
    overrideTextSize = 2;
    overrideX = 10;
    overrideY = 10;
    displayOverride = true;
    displayOverrideUntil = millis() + 30000; // 30s pak zpět na senzory
    drawCustomTextScreen();
  }
  
  // --- CLEAR: Vyčisti displej / zpět na senzory ---
  else if (strcmp(topic, TOPIC_CLEAR) == 0) {
    displayOverride = false;
    display.clearDisplay();
    display.refresh();
    Serial.println("Display cleared");
  }
  
  // --- COMMAND: JSON příkazy ---
  else if (strcmp(topic, TOPIC_COMMAND) == 0) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, message);
    if (err) {
      Serial.printf("JSON parse error: %s\n", err.c_str());
      return;
    }
    
    // Příkaz: zobraz text s parametry
    // {"text":"Hello","x":10,"y":50,"size":3,"duration":60}
    if (doc.containsKey("text")) {
      overrideText = doc["text"].as<String>();
      overrideX = doc["x"] | 10;
      overrideY = doc["y"] | 10;
      overrideTextSize = doc["size"] | 2;
      int duration = doc["duration"] | 30; // sekund
      displayOverride = true;
      displayOverrideUntil = millis() + (duration * 1000UL);
      drawCustomTextScreen();
    }
    
    // Příkaz: zobraz grafické prvky
    // {"line":{"x1":0,"y1":120,"x2":399,"y2":120}}
    if (doc.containsKey("line")) {
      JsonObject line = doc["line"];
      display.drawLine(
        line["x1"] | 0, line["y1"] | 0,
        line["x2"] | 399, line["y2"] | 0, BLACK
      );
      display.refresh();
    }
    
    // Příkaz: obdélník
    // {"rect":{"x":10,"y":10,"w":100,"h":50,"fill":false}}
    if (doc.containsKey("rect")) {
      JsonObject rect = doc["rect"];
      int x = rect["x"] | 0;
      int y = rect["y"] | 0;
      int w = rect["w"] | 50;
      int h = rect["h"] | 30;
      bool fill = rect["fill"] | false;
      if (fill) {
        display.fillRect(x, y, w, h, BLACK);
      } else {
        display.drawRect(x, y, w, h, BLACK);
      }
      display.refresh();
    }
    
    // Příkaz: inverze displeje
    // {"invert":true}
    if (doc.containsKey("invert")) {
      // Sharp LCD nemá HW inverzi, ale můžeme přepsat barvy
      Serial.println("Invert command received");
    }
    
    // Příkaz: přepni zpět na senzorový dashboard
    // {"dashboard":true}
    if (doc.containsKey("dashboard")) {
      displayOverride = false;
      drawSensorScreen();
    }
    
    // Příkaz: nastav interval publikování
    // {"publish_interval":5000}
    // (handled dynamically)
  }
}

// =============================================
//  MQTT - PUBLISH SENSOR DATA
// =============================================

void publishSensorData() {
  if (!mqtt.connected() || !sensorData.valid) return;
  if (firstValidSensorAt == 0 || (millis() - firstValidSensorAt) < appConfig.mqttWarmupDelay) {
    Serial.println("MQTT: warmup delay aktivni, publikace preskocena");
    return;
  }
  
  char buf[16];
  
  // Jednotlivé hodnoty
  snprintf(buf, sizeof(buf), "%.1f", sensorData.temperature);
  mqtt.publish(TOPIC_TEMP, buf, true);
  
  snprintf(buf, sizeof(buf), "%.1f", sensorData.humidity);
  mqtt.publish(TOPIC_HUMIDITY, buf, true);
  
  snprintf(buf, sizeof(buf), "%.1f", sensorData.pm1);
  mqtt.publish(TOPIC_PM1, buf, true);
  
  snprintf(buf, sizeof(buf), "%.1f", sensorData.pm25);
  mqtt.publish(TOPIC_PM25, buf, true);
  
  snprintf(buf, sizeof(buf), "%.1f", sensorData.pm4);
  mqtt.publish(TOPIC_PM4, buf, true);
  
  snprintf(buf, sizeof(buf), "%.1f", sensorData.pm10);
  mqtt.publish(TOPIC_PM10, buf, true);
  
  snprintf(buf, sizeof(buf), "%.0f", sensorData.voc);
  mqtt.publish(TOPIC_VOC, buf, true);
  
  snprintf(buf, sizeof(buf), "%.0f", sensorData.nox);
  mqtt.publish(TOPIC_NOX, buf, true);
  
  snprintf(buf, sizeof(buf), "%u", sensorData.co2);
  mqtt.publish(TOPIC_CO2, buf, true);
  
  // Kompletní JSON
  JsonDocument doc;
  doc["temperature"] = round(sensorData.temperature * 10) / 10.0;
  doc["humidity"]    = round(sensorData.humidity * 10) / 10.0;
  doc["pm1"]  = round(sensorData.pm1 * 10) / 10.0;
  doc["pm25"] = round(sensorData.pm25 * 10) / 10.0;
  doc["pm4"]  = round(sensorData.pm4 * 10) / 10.0;
  doc["pm10"] = round(sensorData.pm10 * 10) / 10.0;
  doc["voc"]  = round(sensorData.voc);
  doc["nox"]  = round(sensorData.nox);
  doc["co2"]  = sensorData.co2;
  doc["quality"] = getAirQuality(sensorData.pm25);
  doc["uptime"]  = millis() / 1000;
  
  char jsonBuf[512];
  serializeJson(doc, jsonBuf, sizeof(jsonBuf));
  mqtt.publish(TOPIC_SENSOR, jsonBuf, true);
  
  Serial.println("MQTT: Sensor data published");
  Serial.printf("MQTT: payload JSON: %s\n", jsonBuf);
}

// =============================================
//  MQTT - HOME ASSISTANT AUTO-DISCOVERY
// =============================================

void publishHADiscovery() {
  // Struktura pro jednotlivé senzory
  struct HASensor {
    const char* name;
    const char* uid;
    const char* topic;
    const char* unit;
    const char* devClass;
    const char* icon;
  };
  
  HASensor sensors[] = {
    {"Teplota",     "sen66_temp",     TOPIC_TEMP,     "°C",     "temperature",  "mdi:thermometer"},
    {"Vlhkost",     "sen66_humidity", TOPIC_HUMIDITY,  "%",      "humidity",     "mdi:water-percent"},
    {"PM1.0",       "sen66_pm1",      TOPIC_PM1,       "µg/m³", "pm1",          "mdi:blur"},
    {"PM2.5",       "sen66_pm25",     TOPIC_PM25,      "µg/m³", "pm25",         "mdi:blur"},
    {"PM4.0",       "sen66_pm4",      TOPIC_PM4,       "µg/m³", NULL,           "mdi:blur-radial"},
    {"PM10",        "sen66_pm10",     TOPIC_PM10,      "µg/m³", "pm10",         "mdi:blur-radial"},
    {"VOC Index",   "sen66_voc",      TOPIC_VOC,       "",       NULL,           "mdi:air-filter"},
    {"NOx Index",   "sen66_nox",      TOPIC_NOX,       "",       NULL,           "mdi:molecule"},
    {"CO2",         "sen66_co2",      TOPIC_CO2,       "ppm",   "carbon_dioxide","mdi:molecule-co2"},
  };
  
  for (auto& s : sensors) {
    JsonDocument doc;
    doc["name"] = s.name;
    doc["unique_id"] = s.uid;
    doc["state_topic"] = s.topic;
    doc["unit_of_measurement"] = s.unit;
    if (s.devClass) doc["device_class"] = s.devClass;
    if (s.icon) doc["icon"] = s.icon;
    doc["availability_topic"] = TOPIC_STATUS;
    doc["payload_available"] = "online";
    doc["payload_not_available"] = "offline";
    
    // Device info
    JsonObject dev = doc["device"].to<JsonObject>();
    dev["identifiers"][0] = "sharp_sen66_esp32c3";
    dev["name"] = "Sharp SEN66 Displej";
    dev["model"] = "ESP32-C3 + SEN66 + Sharp LCD";
    dev["manufacturer"] = "DIY";
    dev["sw_version"] = "2.0.0";
    
    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/config", s.uid);
    
    char payload[512];
    serializeJson(doc, payload, sizeof(payload));
    mqtt.publish(topic, payload, true);
    
    Serial.printf("HA Discovery: %s\n", s.name);
    delay(50); // malý delay mezi zprávami
  }
  
  Serial.println("HA Discovery: Hotovo!");
}

// =============================================
//  MQTT - CONNECT
// =============================================

bool reconnectMQTT() {
  Serial.print("MQTT: Pripojuji...");
  
  // Last will - offline status
  if (mqtt.connect(appConfig.mqttClientId.c_str(), appConfig.mqttUser.c_str(), appConfig.mqttPassword.c_str(),
                    TOPIC_STATUS, 0, true, "offline")) {
    Serial.println("OK!");
    
    // Status online
    mqtt.publish(TOPIC_STATUS, "online", true);
    
    // Subscribe
    mqtt.subscribe(TOPIC_TEXT);
    mqtt.subscribe(TOPIC_CLEAR);
    mqtt.subscribe(TOPIC_COMMAND);
    mqtt.subscribe(TOPIC_BRIGHTNESS);
    
    // HA Auto-Discovery
    publishHADiscovery();
    
    return true;
  } else {
    Serial.printf("CHYBA rc=%d\n", mqtt.state());
    return false;
  }
}

// =============================================
//  WiFi
// =============================================

void setupWiFi() {
  Serial.printf("WiFi: Pripojuji k %s", appConfig.wifiSsid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(appConfig.wifiSsid.c_str(), appConfig.wifiPassword.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi: OK! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nWiFi: CHYBA! Pokracuji bez WiFi...");
  }
}

// =============================================
//  SETUP
// =============================================

void setup() {
  Serial.begin(115200);
  delay(2000); // ESP32-C3 potřebuje čas na USB Serial
  
  Serial.println("\n========================================");
  Serial.println("  Sharp LCD + SEN66 + MQTT v2.0.0");
  Serial.println("========================================\n");
  
  bool configLoaded = loadConfig(appConfig);
  Serial.printf("CFG: load %s\n", configLoaded ? "OK" : "FAILED - defaults");
  Serial.printf("CFG: MQTT %s:%d, MQTT interval=%lu ms, TMEP interval=%lu ms\n", appConfig.mqttServer.c_str(), appConfig.mqttPort, appConfig.mqttPublishInterval, appConfig.tmepRequestInterval);
  Serial.printf("CFG: TMEP domena: %s\n", appConfig.tmepDomain.length() ? appConfig.tmepDomain.c_str() : "(nenastaveno)");

  // 1. Displej
  Serial.println("Display: Inicializace...");
  display.begin();
  applyDisplaySettings();
  display.clearDisplay();
  display.setTextColor(BLACK);
  drawSplashScreen();
  Serial.println("Display: OK!");
  
  // 2. WiFi
  setupWiFi();
  
  // 3. MQTT
  mqtt.setServer(appConfig.mqttServer.c_str(), appConfig.mqttPort);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(1024); // Větší buffer pro HA Discovery JSON
  
  if (WiFi.status() == WL_CONNECTED) {
    reconnectMQTT();
  }
  
  // 4. SEN66
  initSEN66();
  
  // 5. Splash na 2 sekundy
  delay(2000);
  
  Serial.println("\n=== SETUP HOTOV ===\n");
}

// =============================================
//  LOOP
// =============================================

void loop() {
  unsigned long now = millis();
  
  // --- WiFi reconnect ---
  static bool webServerStarted = false;
  if (WiFi.status() != WL_CONNECTED) {
    webServerStarted = false;
    static unsigned long lastWifiRetry = 0;
    if (now - lastWifiRetry > 30000) {
      lastWifiRetry = now;
      Serial.println("WiFi: Reconnecting...");
      WiFi.reconnect();
    }
  } else if (!webServerStarted) {
    setupWebServer();
    webServerStarted = true;
  }
  
  // --- MQTT reconnect ---
  if (WiFi.status() == WL_CONNECTED && !mqtt.connected()) {
    if (now - lastMqttReconnect > MQTT_RECONNECT_INTERVAL) {
      lastMqttReconnect = now;
      reconnectMQTT();
    }
  }
  
  // --- MQTT loop ---
  if (mqtt.connected()) {
    mqtt.loop();
  }

  if (WiFi.status() == WL_CONNECTED) {
    webServer.handleClient();
  }
  
  // --- Čtení SEN66 ---
  if (now - lastSensorRead > SENSOR_READ_INTERVAL) {
    lastSensorRead = now;
    readSEN66();
  }
  
  // --- Publikování do MQTT ---
  if (now - lastMqttPublish > appConfig.mqttPublishInterval) {
    lastMqttPublish = now;
    publishSensorData();
  }

  // --- Odeslani dat na TMEP ---
  if (now - lastTmepRequest > appConfig.tmepRequestInterval) {
    lastTmepRequest = now;
    sendTmepRequest(false);
  }
  
  // --- Override timeout (vrátit se na senzorový dashboard) ---
  if (displayOverride && now > displayOverrideUntil) {
    displayOverride = false;
    Serial.println("Display: Override expired, zpet na dashboard");
  }
  
  // --- Refresh displeje ---
  if (now - lastDisplayRefresh > appConfig.displayRefreshInterval) {
    lastDisplayRefresh = now;
    if (!displayOverride) {
      drawSensorScreen();
    }
  }
  
  delay(10);
}
