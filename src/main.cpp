/*
 * ============================================================
 *  Sharp Memory LCD + SEN66 Sensor + MQTT (Home Assistant)
 *  ESP32-C3-MINI1 | PlatformIO + Arduino Framework
 * ============================================================
 *  Verze: 2.2.1
 *  Datum: 2026-10-19
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
#include <Update.h>
#include <cmath>
#include <limits>
#include "config.h"
#include "WifiProvisioning.h"
#include "DisplayDashboard.h"

#define FW_VERSION "2.2.1"

// =============================================
//  HARDWARE
// =============================================

// SPI piny (Sharp LCD)
#define PIN_SPI_CLK   6
#define PIN_SPI_MOSI  7
#define PIN_SPI_MISO  2   // nepouzity - displej je write-only
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
#define SENSOR_READ_INTERVAL     2000
#define MQTT_RECONNECT_INTERVAL  5000
#define SENSOR_STALE_TIMEOUT     30000  // bez platneho vzorku dele => data neplatna
#define SEN66_REINIT_INTERVAL    30000  // jak casto zkouset znovu inicializovat SEN66
#define SEN66_MAX_READ_ERRORS    10     // po tolika chybach cteni za sebou => reinit

// =============================================
//  GLOBALNI OBJEKTY
// =============================================

Adafruit_SharpMem display(PIN_SPI_CLK, PIN_SPI_MOSI, PIN_SPI_CS,
                          DISPLAY_WIDTH, DISPLAY_HEIGHT);
SensirionI2cSen66 sen66;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
WebServer webServer(80);
WifiProvisioning wifiProvisioning;

AppConfig appConfig;

// =============================================
//  MQTT TOPICS (skladaji se za behu z mqttBaseTopic)
// =============================================

String deviceId;  // posledni 3 byty MAC, napr. "a1b2c3"

String topicText, topicClear, topicCommand;
String topicStatus, topicSensor;
String topicTemp, topicHumidity, topicPm1, topicPm25, topicPm4, topicPm10;
String topicVoc, topicNox, topicCo2;
String topicHaStatus;

void buildTopics() {
  const String& b = appConfig.mqttBaseTopic;

  topicText     = b + "/display/text";
  topicClear    = b + "/display/clear";
  topicCommand  = b + "/display/command";

  topicStatus   = b + "/status";
  topicSensor   = b + "/sensor";
  topicTemp     = b + "/sensor/temperature";
  topicHumidity = b + "/sensor/humidity";
  topicPm1      = b + "/sensor/pm1";
  topicPm25     = b + "/sensor/pm25";
  topicPm4      = b + "/sensor/pm4";
  topicPm10     = b + "/sensor/pm10";
  topicVoc      = b + "/sensor/voc";
  topicNox      = b + "/sensor/nox";
  topicCo2      = b + "/sensor/co2";

  topicHaStatus = appConfig.haPrefix + "/status";
}

// =============================================
//  DATA SENZORU
// =============================================

struct SensorData {
  float pm1   = 0.0f;
  float pm25  = 0.0f;
  float pm4   = 0.0f;
  float pm10  = 0.0f;
  float temperature = 0.0f;
  float humidity    = 0.0f;
  float voc   = 0.0f;
  float nox   = 0.0f;
  uint16_t co2 = 0;
  bool valid    = false;   // teplota / vlhkost / PM / VOC / NOx
  bool co2Valid = false;   // CO2 se validuje zvlast (nabiha pozdeji)
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
unsigned long lastValidSensorAt = 0;
unsigned long lastSen66InitAttempt = 0;
uint8_t sen66ReadErrors = 0;
bool sensorStale = false;          // data zamrzla => HA availability "offline"
bool legacyDiscoveryCleaned = false;

void publishAvailability();  // definovano v sekci MQTT

String lastTmepStatus = "TMEP:---";

bool sen66Ready = false;
bool displayOverride = false;
unsigned long displayOverrideUntil = 0;

String overrideText = "";
int overrideTextSize = 2;
int overrideX = 10;
int overrideY = 10;

// =============================================
//  DISPLAY - BARVY A POMOCNE FUNKCE
// =============================================

// Sharp Memory LCD nema HW inverzi, delame ji softwarove prohozenim barev.
static inline uint16_t inkColor() {
  return appConfig.displayInvertRequested ? WHITE : BLACK;
}

void clearCanvas() {
  display.clearDisplay();
  if (appConfig.displayInvertRequested) {
    display.fillRect(0, 0, display.width(), display.height(), BLACK);
  }
  display.setTextColor(inkColor());
}

void applyDisplaySettings() {
  display.setRotation(appConfig.displayRotation % 4);
  display.setTextColor(inkColor());
  Serial.printf("Display: rotace=%u, inverze=%s\n",
                appConfig.displayRotation % 4,
                appConfig.displayInvertRequested ? "ano (SW)" : "ne");
}

// =============================================
//  VALIDACE DAT SENZORU
// =============================================

static inline bool inRange(float v, float lo, float hi) {
  return !isnan(v) && v >= lo && v <= hi;
}

// Teplota/vlhkost/PM/VOC/NOx. CO2 se resi zvlast, protoze nabiha az po ~1 minute
// a driv vraci 0 nebo 0xFFFF - drive to shodilo cely vzorek vcetne teploty.
bool coreValuesLookValid(float pm1, float pm25, float pm4, float pm10,
                         float hum, float temp, float voc, float nox) {
  if (!inRange(temp, -40.0f, 85.0f)) return false;
  if (!inRange(hum, 0.0f, 100.0f)) return false;
  if (!inRange(pm1, 0.0f, 1000.0f)) return false;
  if (!inRange(pm25, 0.0f, 1000.0f)) return false;
  if (!inRange(pm4, 0.0f, 1000.0f)) return false;
  if (!inRange(pm10, 0.0f, 1000.0f)) return false;
  if (!inRange(voc, 0.0f, 500.0f)) return false;
  if (!inRange(nox, 0.0f, 500.0f)) return false;
  return true;
}

// SEN66 vraci 0xFFFF, dokud CO2 neni k dispozici. Merici rozsah je 400-5000 ppm,
// dolni mez volime 300 kvuli ASC kalibraci, horni 10000 kvuli spickam.
bool co2LooksValid(uint16_t co2) {
  return co2 >= 300 && co2 <= 10000;
}

// =============================================
//  DISPLAY - HLAVNI OBRAZOVKY
// =============================================

// Hodnoceni kvality vzduchu podle PM2.5
const char* getAirQuality(float pm25) {
  static const char* const labels[] = {"VYNIKAJICI", "DOBRE", "PRIJATELNE", "SPATNE", "VELMI SPATNE", "NEBEZPECNE"};
  return labels[dashboard::airQualityBand(pm25)];
}

void drawSensorScreen() {
  String wifiStatus;
  if (WiFi.status() == WL_CONNECTED) wifiStatus = "Wi-Fi " + WiFi.localIP().toString();
  else if (wifiProvisioning.isCaptiveMode()) wifiStatus = "AP " + wifiProvisioning.getApIp();
  else wifiStatus = "Wi-Fi !";
  const dashboard::Data data = {
    sensorData.temperature, sensorData.humidity,
    {sensorData.pm1, sensorData.pm25, sensorData.pm4, sensorData.pm10},
    sensorData.voc, sensorData.nox, sensorData.co2,
    sensorData.valid, sensorData.co2Valid, sen66Ready, sensorStale, mqtt.connected(),
    wifiStatus.c_str(), lastTmepStatus.c_str(), millis() / 1000
  };
  dashboard::draw(display, data, inkColor(), appConfig.displayInvertRequested ? BLACK : WHITE);
  display.refresh();
}

void drawCustomTextScreen() {
  clearCanvas();
  display.setTextSize(overrideTextSize);
  display.setCursor(overrideX, overrideY);
  display.println(overrideText);
  display.refresh();
}

void drawSplashScreen() {
  const String firmware = "FW: " + String(FW_VERSION);
  const String wifi = "Wi-Fi: " + (appConfig.wifiSsid.length() ? appConfig.wifiSsid : String("(nenastaveno)"));
  const String broker = "MQTT: " + (appConfig.mqttServer.length() ? appConfig.mqttServer : String("(nenastaveno)"));
  const char* lines[] = {firmware.c_str(), wifi.c_str(), broker.c_str(), "Inicializace..."};
  dashboard::message(display, "Sharp LCD + SEN66", lines, 4, inkColor(),
                     appConfig.displayInvertRequested ? BLACK : WHITE);
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
  Serial.println("SEN66: Inicializace...");
  sen66ReadErrors = 0;

  sen66.begin(Wire, SEN66_I2C_ADDR_6B);

  int16_t error = sen66.deviceReset();
  if (error != NO_ERROR) {
    char msg[64];
    errorToString(error, msg, sizeof(msg));
    Serial.printf("SEN66: deviceReset() CHYBA: %s\n", msg);
    sen66Ready = false;
    return;
  }

  delay(1200);  // SEN66 potrebuje cas po resetu

  int8_t serialNumber[32] = {0};
  error = sen66.getSerialNumber(serialNumber, 32);
  if (error != NO_ERROR) {
    char msg[64];
    errorToString(error, msg, sizeof(msg));
    Serial.printf("SEN66: getSerialNumber() CHYBA: %s\n", msg);
  } else {
    Serial.printf("SEN66: S/N: %s\n", (const char*)serialNumber);
  }

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
      pm1, pm25, pm4, pm10, hum, temp, voc, nox, co2);

  if (error != NO_ERROR) {
    char msg[64];
    errorToString(error, msg, sizeof(msg));
    Serial.printf("SEN66: readMeasuredValues() CHYBA: %s\n", msg);
    if (++sen66ReadErrors >= SEN66_MAX_READ_ERRORS) {
      Serial.println("SEN66: opakovane chyby cteni, zkusim senzor znovu inicializovat");
      sen66Ready = false;
    }
    return;
  }
  sen66ReadErrors = 0;

  if (!coreValuesLookValid(pm1, pm25, pm4, pm10, hum, temp, voc, nox)) {
    Serial.println("SEN66: neplatna data (T/H/PM/VOC/NOx), vzorek preskocen");
    return;
  }

  sensorData.pm1  = pm1;
  sensorData.pm25 = pm25;
  sensorData.pm4  = pm4;
  sensorData.pm10 = pm10;
  sensorData.temperature = temp + appConfig.temperatureOffset;
  sensorData.humidity    = hum;
  sensorData.voc  = voc;
  sensorData.nox  = nox;
  sensorData.valid = true;

  if (co2LooksValid(co2)) {
    sensorData.co2 = co2;
    sensorData.co2Valid = true;
  } else {
    sensorData.co2Valid = false;
  }

  lastValidSensorAt = millis();
  if (firstValidSensorAt == 0) firstValidSensorAt = lastValidSensorAt;

  if (sensorStale) {
    sensorStale = false;
    Serial.println("SEN66: data opet aktualni");
    publishAvailability();
  }

  Serial.printf("SEN66: T(raw)=%.1f T(adj)=%.1f H=%.1f PM2.5=%.1f VOC=%.0f NOx=%.0f CO2=%s\n",
                temp, sensorData.temperature, hum, pm25, voc, nox,
                sensorData.co2Valid ? String(sensorData.co2).c_str() : "n/a");
}

// Prvni hodnoty po startu/resetu senzoru byvaji nesmyslne - do MQTT ani TMEP
// se posilaji az po uplynuti mqttWarmupDelay od prvniho platneho vzorku.
bool sensorWarmupDone() {
  return firstValidSensorAt != 0 &&
         (millis() - firstValidSensorAt) >= appConfig.mqttWarmupDelay;
}

// =============================================
//  TMEP.CZ
// =============================================

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

  // Hodnoty ze senzoru
  replaceAllTokens(params, "TEMP", formatFloat1(sensorData.temperature));
  replaceAllTokens(params, "HUM", formatFloat1(sensorData.humidity));
  replaceAllTokens(params, "PM1", formatFloat1(sensorData.pm1));
  replaceAllTokens(params, "PM2", formatFloat1(sensorData.pm25));
  replaceAllTokens(params, "PM4", formatFloat1(sensorData.pm4));
  replaceAllTokens(params, "PM10", formatFloat1(sensorData.pm10));
  replaceAllTokens(params, "VOC", formatFloat0(sensorData.voc));
  replaceAllTokens(params, "NOX", formatFloat0(sensorData.nox));
  replaceAllTokens(params, "CO2", sensorData.co2Valid ? String(sensorData.co2) : String(""));

  // Systemove hodnoty (README je dokumentoval, implementace chybela)
  replaceAllTokens(params, "RSSI", WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) : String(""));
  replaceAllTokens(params, "UPTIME", String(millis() / 1000));
  replaceAllTokens(params, "FREEHEAP", String(ESP.getFreeHeap()));
  replaceAllTokens(params, "IP", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String(""));

  return params;
}

String buildTmepRequestUrl() {
  if (appConfig.tmepDomain.length() == 0 || appConfig.tmepParams.length() == 0 || !sensorData.valid) {
    return "";
  }
  return "http://" + appConfig.tmepDomain + ".tmep.cz/?" + buildTmepQueryParams();
}

bool sendTmepRequest(const bool manualTrigger) {
  if (appConfig.tmepDomain.length() == 0 || appConfig.tmepParams.length() == 0) {
    lastTmepStatus = "TMEP:OFF";
    return false;
  }
  if (!sensorData.valid) {
    Serial.println("TMEP: nejsou validni data senzoru, request preskocen");
    lastTmepStatus = "TMEP:SKIP";
    return false;
  }
  if (!manualTrigger && !sensorWarmupDone()) {
    Serial.println("TMEP: warmup delay aktivni, request preskocen");
    lastTmepStatus = "TMEP:WAIT";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("TMEP: WiFi neni pripojena, request preskocen");
    lastTmepStatus = "TMEP:SKIP";
    return false;
  }

  String url = buildTmepRequestUrl();
  if (url.length() == 0) {
    lastTmepStatus = "TMEP:SKIP";
    return false;
  }

  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);
  if (!http.begin(url)) {
    Serial.println("TMEP: Nelze inicializovat HTTP request");
    lastTmepStatus = "TMEP:ERR";
    return false;
  }

  int httpCode = http.GET();
  String response = http.getString();
  http.end();

  if (httpCode > 0 && httpCode < 400) {
    Serial.printf("TMEP: %srequest OK, HTTP %d\n", manualTrigger ? "manual " : "", httpCode);
    lastTmepStatus = "TMEP:OK";
    return true;
  }

  Serial.printf("TMEP: %srequest CHYBA, HTTP %d, body: %s\n",
                manualTrigger ? "manual " : "", httpCode, response.c_str());
  lastTmepStatus = "TMEP:ERR";
  return false;
}

// =============================================
//  WEB SERVER
// =============================================

// Bez hesla jen pri prvotnim nastaveni (AP rezim a zadne ulozene SSID), jinak
// by se uzivatel k zarizeni vubec nedostal. AP fallback po vypadku Wi-Fi je
// heslem chraneny - driv nebyl a otevrena sit tak zpristupnila celou konfiguraci.
bool requireAuth() {
  if (wifiProvisioning.isCaptiveMode() && !wifiProvisioning.hasStoredCredentials()) return false;
  if (appConfig.webPassword.length() == 0) return false;
  if (!webServer.authenticate(appConfig.webUser.c_str(), appConfig.webPassword.c_str())) {
    webServer.requestAuthentication();
    return true;
  }
  return false;
}

void sendJson(int code, JsonDocument& doc) {
  String payload;
  serializeJson(doc, payload);  // String roste dle potreby, nic se neorizne
  webServer.send(code, "application/json", payload);
}

void handleWebRoot() {
  if (requireAuth()) return;

  const char* html = R"HTML(
<!doctype html><html lang="cs"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>SEN66 panel</title>
<style>
*{box-sizing:border-box}body{font-family:system-ui,-apple-system,"Segoe UI",sans-serif;margin:0;background:#f1f5f9;color:#17243b;line-height:1.5}header{background:#0f172a;color:#fff;padding:22px max(20px,calc((100% - 1080px)/2))}header h2{margin:0;font-size:22px;letter-spacing:-.5px}header p{margin:4px 0 0;color:#a8b9d0;font-size:13px}main{padding:24px 20px;max-width:1120px;margin:0 auto}
.tabs{display:flex;gap:6px;margin-bottom:20px;flex-wrap:wrap}.tab{font:inherit;font-size:14px;font-weight:600;padding:11px 18px;border:1px solid transparent;border-radius:10px;background:#e2e8f0;color:#475569;cursor:pointer}.tab.active{background:#2563eb;color:#fff;box-shadow:0 3px 8px #2563eb26}.tab:hover{filter:brightness(.96)}button:focus-visible,input:focus-visible,summary:focus-visible,a:focus-visible{outline:3px solid #2563eb;outline-offset:3px}
.panel{display:none;background:#fff;padding:24px;border:1px solid #e2e8f0;border-radius:16px;box-shadow:0 4px 20px #0f172a06}.panel.active{display:block}
.data-heading{display:flex;justify-content:space-between;gap:12px;align-items:center;margin-bottom:22px}.data-heading h1{font-size:23px;letter-spacing:-.6px;margin:0}.data-heading p{margin:3px 0 0}.live{font-size:12px;color:#475569;background:#f1f5f9;border-radius:20px;padding:6px 12px;white-space:nowrap}
.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:14px}.card{--tone:#64748b;--wash:#f8fafc;--edge:#e2e8f0;min-width:0;border:1px solid var(--edge);border-top:3px solid var(--tone);border-radius:12px;padding:17px 18px;background:var(--wash)}
.card.good{--tone:#166534;--wash:#f0fdf4;--edge:#bbdfc5}.card.watch{--tone:#854d0e;--wash:#fffbeb;--edge:#f1d690}.card.high{--tone:#9a3412;--wash:#fff7ed;--edge:#fdcba0}.card.bad{--tone:#991b1b;--wash:#fef2f2;--edge:#fecaca}
.card-label{margin:0;font-size:14px;font-weight:650}.card-description{font-size:12px;color:#526175;margin:3px 0 12px}.reading{display:flex;align-items:baseline;gap:7px;flex-wrap:wrap}.value{font-size:36px;line-height:1.2;letter-spacing:-1px;font-weight:650;font-variant-numeric:tabular-nums}.unit{font-size:14px;color:#526175}.assessment{display:inline-block;margin-top:13px;font-size:12px;font-weight:600;color:var(--tone)}.assessment:before{content:"";display:inline-block;width:7px;height:7px;border-radius:50%;background:var(--tone);margin-right:6px}
.device-status{display:flex;gap:8px;flex-wrap:wrap;padding-top:18px;margin-top:22px;border-top:1px solid #e2e8f0}.status-item{font-size:12px;background:#f1f5f9;border-radius:7px;padding:6px 10px;overflow-wrap:anywhere}.status-item.ok{background:#f0fdf4}.status-item.err{background:#fef2f2}.device-meta{display:flex;justify-content:space-between;flex-wrap:wrap;gap:8px;margin-top:12px}.legend{margin-top:20px;border-top:1px solid #e2e8f0;padding-top:16px;font-size:12px;color:#526175}.legend summary{cursor:pointer;font-weight:600}.legend p{margin:10px 0 0}.legend a{color:#1d4ed8}
@media(max-width:700px){.grid{grid-template-columns:repeat(2,minmax(0,1fr))}.panel{padding:18px}.data-heading{align-items:flex-start;flex-direction:column}.value{font-size:30px}}@media(max-width:380px){main{padding:16px 10px}.panel{padding:14px}.grid{grid-template-columns:1fr}.tab{padding:10px 12px}}
label{display:block;font-size:14px;margin-top:8px}input{width:100%;padding:8px;border:1px solid #cbd5e1;border-radius:6px;box-sizing:border-box}
button.save,button.secondary,button.warn{margin-top:12px;padding:10px 14px;color:#fff;border:0;border-radius:8px;cursor:pointer}
button.save{background:#16a34a}button.secondary{background:#2563eb}button.warn{background:#b91c1c}
label.chk{display:flex;align-items:center;gap:6px}label.chk input{width:auto}progress{width:100%;height:18px;margin-top:10px}
.muted{color:#666;font-size:13px}.ok{color:#166534}.err{color:#b91c1c}code.url{display:block;padding:8px;background:#f1f5f9;border-radius:6px;word-break:break-all;font-size:13px}
</style></head><body><header><h2>SEN66 <span style="font-weight:400;color:#a8b9d0">/ monitor vzduchu</span></h2><p>Teplota, vlhkost a kvalita vzduchu v místnosti</p></header><main>
<div class="tabs"><button class="tab active" data-tab="data">Aktuální data</button><button class="tab" data-tab="cfg">Konfigurace</button><button class="tab" data-tab="fw">Aktualizace</button></div>
<section id="data" class="panel active" aria-labelledby="dataTitle">
<div class="data-heading"><div><h1 id="dataTitle">Aktuální měření</h1><p class="muted">Aktuální hodnoty ze senzoru SEN66</p></div><span id="dataState" class="live" role="status">Načítám měření…</span></div>
<div class="grid" id="cards"></div><div class="device-status" id="status"></div>
<div class="device-meta muted"><span id="updated">Čekám na první data</span><span id="deviceMeta"></span></div>
<details class="legend"><summary>Jak číst hodnoty a barvy</summary>
<p>Barvy jsou orientační pomůcka pro aktuální měření, nikoli zdravotní limity nebo výpočet AQI z denních průměrů. Zelená značí nízkou hodnotu, žlutá zvýšenou, oranžová vysokou a červená velmi vysokou. Šedá značí nehodnocenou nebo nedostupnou hodnotu.</p>
<p>CO₂: pod 1 000 ppm nízká hodnota, 1 000–1 999 ppm doporučení vyvětrat, od 2 000 ppm výrazné doporučení vyvětrat. <a href="https://www.umweltbundesamt.de/en/node/83460" target="_blank" rel="noopener noreferrer">Podklad pro CO₂</a>.</p>
<p>Prachové částice: PM2,5 má barevné hranice 12 / 35,4 / 55,4 µg/m³, PM10 má hranice 54 / 154 / 254 µg/m³. Jde o orientační stupnici tohoto rozhraní. PM1 a PM4 zobrazujeme bez hodnocení, protože pro ně nepoužíváme samostatnou stupnici.</p>
<p>VOC a NOₓ jsou bezrozměrné indexy 1–500, nikoli koncentrace v ppm či µg/m³. VOC má běžnou úroveň kolem 100, NOₓ kolem 1. Barevné hranice rozhraní: VOC 150 / 250 / 350, NOₓ 20 / 50 / 100. Nula znamená, že index ještě není připraven. <a href="https://sensirion.com/media/documents/02232963/6294E043/Info_Note_VOC_Index.pdf" target="_blank" rel="noopener noreferrer">VOC index</a> · <a href="https://sensirion.com/media/documents/9F289B95/6294DFFC/Info_Note_NOx_Index.pdf" target="_blank" rel="noopener noreferrer">NOₓ index</a>.</p>
</details></section>
<section id="cfg" class="panel"><form id="cfgForm"><h3>Wi-Fi</h3>
<p class="muted" id="wifiMode"></p><p class="muted" id="wifiConn"></p>
<label>SSID<input name="wifiSsid" required></label>
<label>Heslo<input type="password" name="wifiPassword" id="wifiPass" placeholder="(beze změny)"></label>
<label class="chk"><input id="showPass" type="checkbox"> Zobrazit heslo</label>
<label class="chk"><input id="clrWifi" type="checkbox" data-for="wifiPassword"> Smazat heslo (otevřená síť)</label>
<button id="wifiOnlySaveBtn" class="secondary" type="button">Uložit jen Wi-Fi a restartovat</button>
<button id="wifiForgetBtn" class="warn" type="button">Zapomenout Wi-Fi</button><p class="muted" id="wifiMsg"></p>
<h3>MQTT</h3><label>Server<input name="mqttServer" placeholder="192.168.1.10"></label><label>Port<input type="number" min="1" max="65535" name="mqttPort" required></label><label>Uživatel<input name="mqttUser"></label><label>Heslo<input type="password" name="mqttPassword" placeholder="(beze změny)"></label><label class="chk"><input id="clrMqtt" type="checkbox" data-for="mqttPassword"> Smazat heslo</label>
<label>Client ID<input name="mqttClientId"></label><label>Base topic<input name="mqttBaseTopic" placeholder="sharp"></label><label>Discovery prefix Home Assistantu<input name="haPrefix" placeholder="homeassistant"></label>
<h3>Zabezpečení web UI</h3><label>Uživatel<input name="webUser" placeholder="admin"></label><label>Heslo (prázdné pole = beze změny)<input type="password" name="webPassword" placeholder="(beze změny)"></label><label class="chk"><input id="clrWeb" type="checkbox" data-for="webPassword"> Smazat heslo (web UI bude bez přihlášení)</label>
<h3>TMEP.cz</h3><label>Doména pro zasílání hodnot<input name="tmepDomain" placeholder="xxk4sk-g6rxfh"></label><label>Parametry požadavku<input name="tmepParams" placeholder="tempV=*TEMP*&humV=*HUM*&co2=*CO2*"></label>
<p class="muted">Proměnné: *TEMP*, *HUM*, *PM1*, *PM2*, *PM4*, *PM10*, *VOC*, *NOX*, *CO2*, *RSSI*, *UPTIME*, *FREEHEAP*, *IP*.</p><p class="muted">Reálné URL volané na TMEP.cz:</p><code id="tmepUrl" class="url muted">Není dostupné</code>
<button id="tmepSendBtn" class="secondary" type="button">Odeslat TMEP request ručně</button><p id="tmepMsg" class="muted"></p>
<h3>Displej</h3><label>Rotace (0-3)<input type="number" min="0" max="3" name="displayRotation" required></label><label>Inverze (0/1)<input type="number" min="0" max="1" name="displayInvertRequested" required></label>
<h3>Intervaly (ms)</h3><label>Překreslení displeje<input type="number" min="500" name="displayRefreshInterval" required></label><label>MQTT publish<input type="number" min="1000" name="mqttPublishInterval" required></label><label>TMEP request interval<input type="number" min="1000" name="tmepRequestInterval" required></label><label>MQTT warmup delay<input type="number" min="1000" name="mqttWarmupDelay" required></label>
<h3>Kalibrace</h3><label>Teplotní offset (°C)<input type="number" step="0.1" min="-20" max="20" name="temperatureOffset" required></label><p class="muted">Hodnota se přičítá k naměřené teplotě.</p>
<button class="save" type="submit">Uložit plnou konfiguraci</button><p id="cfgMsg" class="muted"></p></form></section>
<section id="fw" class="panel"><h3>Aktualizace firmware (OTA)</h3><p>Aktuální verze: <strong id="fwVer">?</strong></p>
<p class="muted">Nahrajte soubor <code>firmware.bin</code> z GitHub release. Soubor <code>factory.bin</code> je jen pro nahrání kabelem. Konfigurace zůstane zachována.</p>
<input type="file" id="otaFile" accept=".bin"><button id="otaBtn" class="secondary" type="button">Nahrát a restartovat</button>
<progress id="otaProg" max="100" value="0"></progress><p id="otaMsg" class="muted"></p></section></main>
<script>
const PWD=[['wifiPassword','clrWifi'],['mqttPassword','clrMqtt'],['webPassword','clrWeb']];
const NUM=['mqttPort','displayRotation','displayInvertRequested','displayRefreshInterval','mqttPublishInterval','tmepRequestInterval','mqttWarmupDelay','temperatureOffset'];
const tabs=document.querySelectorAll('.tab');tabs.forEach(t=>t.onclick=()=>{tabs.forEach(x=>x.classList.remove('active'));document.querySelectorAll('.panel').forEach(p=>p.classList.remove('active'));t.classList.add('active');document.getElementById(t.dataset.tab).classList.add('active')});
function setMsg(id,text,ok){const m=document.getElementById(id);m.textContent=text;m.className=ok?'ok':'err'}
let otaBusy=false;
// UI thresholds are indicative instantaneous bands, not health limits or AQI.
const METRICS=[
{key:'co2',label:'Oxid uhličitý · CO₂',description:'Ukazatel větrání místnosti',unit:'ppm',digits:0,bands:[1000,2000],labels:['Nízká hodnota','Doporučujeme vyvětrat','Vyvětrejte místnost']},
{key:'temperature',label:'Teplota',description:'Teplota vzduchu',unit:'°C',digits:1},
{key:'humidity',label:'Relativní vlhkost',description:'Vlhkost vzduchu',unit:'%',digits:1},
{key:'pm25',label:'Prachové částice · PM2,5',description:'Částice do velikosti 2,5 µm',unit:'µg/m³',digits:1,bands:[12,35.4,55.4]},
{key:'pm10',label:'Prachové částice · PM10',description:'Částice do velikosti 10 µm',unit:'µg/m³',digits:1,bands:[54,154,254]},
{key:'voc',label:'Těkavé organické látky · VOC',description:'Relativní index vůči běžné úrovni',unit:'index',digits:0,bands:[150,250,350]},
{key:'pm1',label:'Prachové částice · PM1',description:'Částice do velikosti 1 µm',unit:'µg/m³',digits:1},
{key:'pm4',label:'Prachové částice · PM4',description:'Částice do velikosti 4 µm',unit:'µg/m³',digits:1},
{key:'nox',label:'Oxidy dusíku · NOₓ',description:'Relativní index vůči běžné úrovni',unit:'index',digits:0,bands:[20,50,100]}
];
const cardNodes=METRICS.map(m=>{const card=document.createElement('article');card.className='card';
const label=document.createElement('h2');label.className='card-label';label.textContent=m.label;
const description=document.createElement('p');description.className='card-description';description.textContent=m.description;
const reading=document.createElement('div');reading.className='reading';const value=document.createElement('span');value.className='value';value.textContent='—';
const unit=document.createElement('span');unit.className='unit';unit.textContent=m.unit;reading.append(value,unit);
const assessment=document.createElement('span');assessment.className='assessment';assessment.textContent='Čekám na data';
card.append(label,description,reading,assessment);document.getElementById('cards').appendChild(card);return {card,value,assessment}});
function classify(m,v){if(!m.bands)return {tone:'',text:m.key.startsWith('pm')?'Bez barevného hodnocení':'Aktuální měření'};
const index=m.bands.filter(limit=>v>=limit).length;
return {tone:(m.bands.length===2?['good','watch','bad']:['good','watch','high','bad'])[index],text:(m.labels||['Nízká hodnota','Zvýšená hodnota','Vysoká hodnota','Velmi vysoká hodnota'])[index]}}
function renderReadings(d,offline=false){METRICS.forEach((m,i)=>{const n=cardNodes[i];const v=d.values&&d.values[m.key];
const ready=!offline&&d.valid===true&&typeof v==='number'&&Number.isFinite(v)&&(!(m.key==='voc'||m.key==='nox')||v>=1);
const state=ready?classify(m,v):{tone:'',text:offline?'Spojení přerušeno':d.valid?'Čekám na měření':'Data nejsou dostupná'};
n.card.className='card'+(state.tone?' '+state.tone:'');n.value.textContent=ready?v.toLocaleString('cs-CZ',{minimumFractionDigits:m.digits,maximumFractionDigits:m.digits}):'—';n.assessment.textContent=state.text})}
function renderStatus(d){const status=document.getElementById('status');status.replaceChildren();
const mode={WIFI_STA_CONNECTED:'Domácí síť',WIFI_STA_CONNECTING:'Připojování k síti',WIFI_AP_CAPTIVE:'Přístupový bod'};
for(const [label,text,tone] of [['Wi-Fi',d.wifi==='connected'?'Připojeno':'Odpojeno',d.wifi==='connected'?'ok':'err'],['MQTT',d.mqtt==='connected'?'Připojeno':'Odpojeno',d.mqtt==='connected'?'ok':'err'],['TMEP',String(d.tmepStatus||'Bez hlášení').replace(/^TMEP:/,''),''],['Režim',mode[d.wifiMode]||d.wifiMode||'Neznámý','']]){
const item=document.createElement('span');item.className='status-item '+tone;item.textContent=label+': '+text;status.appendChild(item)}
const seconds=Math.max(0,Math.floor(Number(d.uptime)||0));const hours=Math.floor(seconds/3600);const minutes=Math.floor(seconds%3600/60);
document.getElementById('deviceMeta').textContent='Doba běhu: '+(hours?hours+' h ':'')+minutes+' min '+seconds%60+' s · FW '+d.fw}
let dataLoading=false;
async function loadData(){if(otaBusy||dataLoading)return;dataLoading=true;const controller=new AbortController();const timeout=setTimeout(()=>controller.abort(),8000);try{const r=await fetch('/api/data',{cache:'no-store',signal:controller.signal});if(!r.ok)throw new Error('HTTP '+r.status);const d=await r.json();if(!d.values||typeof d.valid!=='boolean')throw new Error('Invalid data');renderReadings(d);renderStatus(d);
document.getElementById('dataState').textContent=d.valid?'Obnova každé 2 s':'Čekám na data senzoru';
document.getElementById('updated').textContent='Poslední spojení: '+new Date().toLocaleTimeString('cs-CZ');
document.getElementById('fwVer').textContent=d.fw;
document.getElementById('wifiMode').textContent='Režim: '+d.wifiMode+(d.apSsid?(' | AP: '+d.apSsid+' @ '+d.apIp):'');
document.getElementById('wifiConn').textContent='Aktuální SSID: '+(d.currentSsid||'-')+' | IP: '+(d.currentIp||'-')+' | RSSI: '+(d.rssi||'-')+' dBm';
const u=document.getElementById('tmepUrl');u.textContent=d.tmepUrl||'Není dostupné';u.className=d.tmepUrl?'url':'url muted'}catch(e){renderReadings({},true);document.getElementById('dataState').textContent='Zařízení není dostupné';const status=document.getElementById('status');status.textContent='Spojení se zařízením selhalo. Zkouším znovu každé 2 s.';document.getElementById('deviceMeta').textContent=''}finally{clearTimeout(timeout);dataLoading=false}}
async function loadCfg(){const r=await fetch('/api/config');const c=await r.json();const f=document.getElementById('cfgForm');Object.keys(c).forEach(k=>{const el=f[k];if(el&&'value' in el)el.value=c[k]});
PWD.forEach(([k])=>{f[k].placeholder=c[k+'Set']?'(nastaveno – beze změny)':'(nenastaveno)'})}
document.querySelectorAll('input[data-for]').forEach(cb=>cb.onchange=()=>{const el=document.getElementById('cfgForm')[cb.dataset.for];el.disabled=cb.checked;if(cb.checked)el.value=''});
function buildPayload(f){const p=Object.fromEntries(new FormData(f).entries());
NUM.forEach(k=>{if(p[k]!==undefined&&p[k]!=='')p[k]=Number(p[k]);else delete p[k]});
// Heslo: prazdne pole = beze zmeny, zaskrtnute "Smazat" = poslat prazdny retezec (smaze se).
PWD.forEach(([k,c])=>{if(document.getElementById(c).checked)p[k]='';else if(p[k]===''||p[k]===undefined)delete p[k]});
return p}
document.getElementById('showPass').onchange=(e)=>{document.getElementById('wifiPass').type=e.target.checked?'text':'password'};
document.getElementById('cfgForm').onsubmit=async(e)=>{e.preventDefault();const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(buildPayload(e.target))});setMsg('cfgMsg',await r.text(),r.ok)};
document.getElementById('wifiOnlySaveBtn').onclick=async()=>{const f=document.getElementById('cfgForm');const p={wifiSsid:f.wifiSsid.value};if(document.getElementById('clrWifi').checked)p.wifiPassword='';else if(f.wifiPassword.value!=='')p.wifiPassword=f.wifiPassword.value;const r=await fetch('/api/wifi/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(p)});const d=await r.json();setMsg('wifiMsg',d.message||'?',r.ok)};
document.getElementById('wifiForgetBtn').onclick=async()=>{if(!confirm('Opravdu smazat uložené Wi-Fi údaje a restartovat do AP režimu?'))return;const r=await fetch('/api/wifi/forget',{method:'POST'});const d=await r.json();setMsg('wifiMsg',d.message||'?',r.ok)};
document.getElementById('tmepSendBtn').onclick=async()=>{const r=await fetch('/api/tmep/send',{method:'POST'});setMsg('tmepMsg',await r.text(),r.ok);await loadData()};
document.getElementById('otaBtn').onclick=()=>{const file=document.getElementById('otaFile').files[0];
if(!file){setMsg('otaMsg','Vyberte soubor firmware.bin',false);return}
if(/factory/i.test(file.name)){setMsg('otaMsg','factory.bin je určen pro nahrání kabelem, pro OTA použijte firmware.bin',false);return}
const fd=new FormData();fd.append('firmware',file,file.name);const x=new XMLHttpRequest();const prog=document.getElementById('otaProg');
x.open('POST','/update');x.upload.onprogress=e=>{if(e.lengthComputable)prog.value=Math.round(e.loaded/e.total*100)};
x.onload=()=>{otaBusy=false;setMsg('otaMsg',x.responseText,x.status===200);if(x.status===200){setMsg('otaMsg',x.responseText+' Stránka se za 20 s obnoví.',true);setTimeout(()=>location.reload(),20000)}};
x.onerror=()=>{otaBusy=false;setMsg('otaMsg','Přenos selhal',false)};
otaBusy=true;prog.value=0;setMsg('otaMsg','Nahrávám '+file.name+'…',true);x.send(fd)};
loadData();loadCfg();setInterval(loadData,2000);
</script></body></html>)HTML";

  webServer.send(200, "text/html; charset=utf-8", html);
}

void handleApiData() {
  if (requireAuth()) return;

  JsonDocument doc;
  doc["fw"] = FW_VERSION;
  doc["wifi"] = WiFi.status() == WL_CONNECTED ? "connected" : "disconnected";
  doc["mqtt"] = mqtt.connected() ? "connected" : "disconnected";
  doc["valid"] = sensorData.valid;
  doc["uptime"] = millis() / 1000;
  doc["tmepUrl"] = buildTmepRequestUrl();
  doc["tmepStatus"] = lastTmepStatus;
  doc["wifiMode"] = wifiProvisioning.getStateText();
  doc["apSsid"] = wifiProvisioning.isCaptiveMode() ? wifiProvisioning.getApSsid() : "";
  doc["apIp"] = wifiProvisioning.isCaptiveMode() ? wifiProvisioning.getApIp() : "";
  doc["currentSsid"] = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "";
  doc["currentIp"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
  doc["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;

  JsonObject values = doc["values"].to<JsonObject>();
  values["temperature"] = round(sensorData.temperature * 10) / 10.0;
  values["humidity"] = round(sensorData.humidity * 10) / 10.0;
  values["pm1"] = round(sensorData.pm1 * 10) / 10.0;
  values["pm25"] = round(sensorData.pm25 * 10) / 10.0;
  values["pm4"] = round(sensorData.pm4 * 10) / 10.0;
  values["pm10"] = round(sensorData.pm10 * 10) / 10.0;
  values["voc"] = round(sensorData.voc);
  values["nox"] = round(sensorData.nox);
  if (sensorData.co2Valid) {
    values["co2"] = sensorData.co2;
  } else {
    values["co2"] = "---";
  }

  sendJson(200, doc);
}

void handleApiConfigGet() {
  if (requireAuth()) return;

  JsonDocument doc;
  doc["wifiSsid"] = appConfig.wifiSsid;
  doc["mqttServer"] = appConfig.mqttServer;
  doc["mqttPort"] = appConfig.mqttPort;
  doc["mqttUser"] = appConfig.mqttUser;
  doc["mqttClientId"] = appConfig.mqttClientId;
  doc["mqttBaseTopic"] = appConfig.mqttBaseTopic;
  doc["haPrefix"] = appConfig.haPrefix;
  doc["webUser"] = appConfig.webUser;
  doc["tmepDomain"] = appConfig.tmepDomain;
  doc["tmepParams"] = appConfig.tmepParams;
  doc["displayRotation"] = appConfig.displayRotation;
  doc["displayInvertRequested"] = appConfig.displayInvertRequested ? 1 : 0;
  doc["displayRefreshInterval"] = appConfig.displayRefreshInterval;
  doc["mqttPublishInterval"] = appConfig.mqttPublishInterval;
  doc["tmepRequestInterval"] = appConfig.tmepRequestInterval;
  doc["mqttWarmupDelay"] = appConfig.mqttWarmupDelay;
  doc["temperatureOffset"] = appConfig.temperatureOffset;

  // Hesla se zasadne neposilaji zpet, jen priznak, zda jsou nastavena.
  doc["wifiPasswordSet"] = appConfig.wifiPassword.length() > 0;
  doc["mqttPasswordSet"] = appConfig.mqttPassword.length() > 0;
  doc["webPasswordSet"] = appConfig.webPassword.length() > 0;

  sendJson(200, doc);
}

// HTML formular posila hodnoty i jako retezce. ArduinoJson 7 vraci u
// `variant | default` default, pokud typ nesedi - proto explicitni prevod.
// Chybejici klic nebo prazdny retezec = beze zmeny (vraci true, `out` nemeni).
// Vraci false pro nesmysl ("abc"), desetinne cislo u celociselne polozky nebo
// hodnotu mimo rozsah typu - drive se napr. -1 u intervalu pretocilo na
// 4294967295 a validaci proslo.
template <typename T>
bool readNumber(JsonVariantConst v, T& out) {
  if (v.isNull()) return true;

  double d;
  if (v.is<const char*>()) {
    String s = v.as<String>();
    s.trim();
    if (s.length() == 0) return true;
    char* end = nullptr;
    d = strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0') return false;
  } else if (v.is<long>()) {
    d = (double)v.as<long>();
  } else if (v.is<double>()) {
    d = v.as<double>();
  } else {
    return false;
  }

  if (!std::isfinite(d)) return false;
  if (std::numeric_limits<T>::is_integer) {
    if (d != std::floor(d)) return false;
    if (d < (double)std::numeric_limits<T>::min() ||
        d > (double)std::numeric_limits<T>::max()) return false;
  }
  out = (T)d;
  return true;
}

String readString(JsonVariantConst v, const String& fallback) {
  if (v.is<const char*>()) return v.as<String>();
  return fallback;
}

void handleApiConfigPost() {
  if (requireAuth()) return;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, webServer.arg("plain"));
  if (err) {
    webServer.send(400, "text/plain", "Neplatny JSON");
    return;
  }

  AppConfig updated = appConfig;

  updated.wifiSsid      = readString(doc["wifiSsid"], updated.wifiSsid);
  updated.mqttServer    = readString(doc["mqttServer"], updated.mqttServer);
  updated.mqttUser      = readString(doc["mqttUser"], updated.mqttUser);
  updated.mqttClientId  = readString(doc["mqttClientId"], updated.mqttClientId);
  updated.mqttBaseTopic = readString(doc["mqttBaseTopic"], updated.mqttBaseTopic);
  updated.haPrefix      = readString(doc["haPrefix"], updated.haPrefix);
  updated.webUser       = readString(doc["webUser"], updated.webUser);
  updated.tmepDomain    = readString(doc["tmepDomain"], updated.tmepDomain);
  updated.tmepParams    = readString(doc["tmepParams"], updated.tmepParams);

  // Hesla: klic nedorazi => beze zmeny, dorazi prazdny retezec => smazat.
  if (!doc["wifiPassword"].isNull()) updated.wifiPassword = doc["wifiPassword"].as<String>();
  if (!doc["mqttPassword"].isNull()) updated.mqttPassword = doc["mqttPassword"].as<String>();
  if (!doc["webPassword"].isNull()) updated.webPassword = doc["webPassword"].as<String>();

  int rotation = updated.displayRotation;
  int invert = updated.displayInvertRequested ? 1 : 0;

  bool numbersOk = true;
  numbersOk &= readNumber(doc["mqttPort"], updated.mqttPort);
  numbersOk &= readNumber(doc["displayRotation"], rotation);
  numbersOk &= readNumber(doc["displayInvertRequested"], invert);
  numbersOk &= readNumber(doc["displayRefreshInterval"], updated.displayRefreshInterval);
  numbersOk &= readNumber(doc["mqttPublishInterval"], updated.mqttPublishInterval);
  numbersOk &= readNumber(doc["tmepRequestInterval"], updated.tmepRequestInterval);
  numbersOk &= readNumber(doc["mqttWarmupDelay"], updated.mqttWarmupDelay);
  numbersOk &= readNumber(doc["temperatureOffset"], updated.temperatureOffset);

  if (!numbersOk || rotation < 0 || rotation > 3 || (invert != 0 && invert != 1)) {
    webServer.send(400, "text/plain", "Neplatne ciselne hodnoty konfigurace");
    return;
  }
  updated.displayRotation = (uint8_t)rotation;
  updated.displayInvertRequested = invert == 1;

  if (!validateConfig(updated)) {
    webServer.send(400, "text/plain", "Neplatne hodnoty konfigurace");
    return;
  }

  if (!saveConfig(updated)) {
    webServer.send(500, "text/plain", "Nepodarilo se ulozit konfiguraci");
    return;
  }

  appConfig = updated;
  webServer.send(200, "text/plain", "Konfigurace ulozena, zarizeni se restartuje...");
  delay(300);
  ESP.restart();
}

void handleApiWifiSave() {
  if (requireAuth()) return;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, webServer.arg("plain"));
  JsonDocument out;

  if (err) {
    out["ok"] = false;
    out["message"] = "Neplatny JSON";
    sendJson(400, out);
    return;
  }

  AppConfig updated = appConfig;
  updated.wifiSsid = doc["wifiSsid"].as<String>();
  if (!doc["wifiPassword"].isNull()) updated.wifiPassword = doc["wifiPassword"].as<String>();

  if (updated.wifiSsid.length() == 0) {
    out["ok"] = false;
    out["message"] = "SSID nesmi byt prazdne";
    sendJson(400, out);
    return;
  }

  if (!saveConfig(updated)) {
    out["ok"] = false;
    out["message"] = "Nepodarilo se ulozit WiFi konfiguraci";
    sendJson(500, out);
    return;
  }

  appConfig = updated;
  out["ok"] = true;
  out["message"] = "Wi-Fi ulozena, restartuji a pripojuji se";
  sendJson(200, out);

  // Odpovime driv, nez zacneme cokoli blokujiciho - drive se 20s pokus
  // o pripojeni delal primo v handleru a prohlizeci vyprsel fetch.
  delay(300);
  ESP.restart();
}

void handleApiWifiForget() {
  if (requireAuth()) return;

  AppConfig updated = appConfig;
  updated.wifiSsid = "";
  updated.wifiPassword = "";
  const bool ok = saveConfig(updated);
  if (ok) appConfig = updated;

  JsonDocument out;
  out["ok"] = ok;
  out["message"] = ok ? "Wi-Fi zapomenuta, restartuji do captive AP"
                      : "Nepodarilo se zapomenout Wi-Fi";
  sendJson(ok ? 200 : 500, out);

  if (ok) {
    delay(300);
    ESP.restart();
  }
}

void handleApiTmepSend() {
  if (requireAuth()) return;

  if (sendTmepRequest(true)) {
    webServer.send(200, "text/plain", "TMEP request byl uspesne odeslan");
    return;
  }
  webServer.send(500, "text/plain",
                 "TMEP request se nepodarilo odeslat (zkontrolujte domenu, WiFi a data senzoru)");
}

// =============================================
//  OTA AKTUALIZACE PRES WEB UI
// =============================================

// OTA jen v rezimu pripojeni k Wi-Fi. V AP rezimu je sit otevrena a pri
// prvotnim nastaveni i bez hesla - nahrani firmware by tam mohl kdokoli.
bool otaAllowedNow() {
  return wifiProvisioning.getState() == WIFI_STA_CONNECTED;
}

String otaError;
bool otaInProgress = false;

void drawOtaScreen(const char* msg) {
  displayOverride = true;
  displayOverrideUntil = millis() + 300000UL;  // dashboard neprekresli hlasku
  const char* lines[] = {msg};
  dashboard::message(display, "Aktualizace FW", lines, 1, inkColor(),
                     appConfig.displayInvertRequested ? BLACK : WHITE);
  display.refresh();
}

// Volano pro kazdy blok nahravaneho souboru. Hlavicky (vcetne autentizace) uz
// jsou v tu chvili zpracovane, takze heslo se overuje hned na zacatku a bez
// nej se do flash nic nezapise.
void handleOtaUpload() {
  HTTPUpload& up = webServer.upload();

  switch (up.status) {
    case UPLOAD_FILE_START:
      otaError = "";
      otaInProgress = false;
      if (!otaAllowedNow()) {
        otaError = "OTA je dostupne jen pri pripojeni k Wi-Fi";
        return;
      }
      if (appConfig.webPassword.length() &&
          !webServer.authenticate(appConfig.webUser.c_str(), appConfig.webPassword.c_str())) {
        otaError = "Neautorizovano";
        return;
      }
      if (up.filename.indexOf("factory") >= 0) {
        otaError = "factory.bin je urcen pro nahrani kabelem, pro OTA pouzijte firmware.bin";
        return;
      }
      Serial.printf("OTA: start, soubor %s\n", up.filename.c_str());
      drawOtaScreen("Nahravam firmware...");
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        otaError = String("Nelze zahajit aktualizaci: ") + Update.errorString();
        return;
      }
      otaInProgress = true;
      break;

    case UPLOAD_FILE_WRITE:
      if (!otaInProgress) return;
      if (Update.write(up.buf, up.currentSize) != up.currentSize) {
        otaError = String("Zapis selhal: ") + Update.errorString();
        Update.abort();
        otaInProgress = false;
      }
      break;

    case UPLOAD_FILE_END:
      if (!otaInProgress) return;
      otaInProgress = false;
      if (!Update.end(true)) {
        otaError = String("Overeni firmware selhalo: ") + Update.errorString();
      } else {
        Serial.printf("OTA: prijato %u B, firmware overen\n", (unsigned)up.totalSize);
      }
      break;

    case UPLOAD_FILE_ABORTED:
    default:
      if (otaInProgress) Update.abort();
      otaInProgress = false;
      otaError = "Prenos byl prerusen";
      break;
  }
}

void handleOtaDone() {
  if (!otaAllowedNow()) {
    webServer.send(403, "text/plain", "OTA je dostupne jen pri pripojeni k Wi-Fi");
    return;
  }
  if (requireAuth()) return;

  if (otaError.length() || !Update.isFinished()) {
    const String msg = otaError.length() ? otaError : String("Firmware nebyl prijat");
    Serial.printf("OTA: CHYBA - %s\n", msg.c_str());
    displayOverride = false;  // zpet na dashboard
    webServer.send(500, "text/plain", msg);
    return;
  }

  webServer.send(200, "text/plain", "Firmware nahran a overen, restartuji...");
  drawOtaScreen("Hotovo, restart...");
  delay(500);
  ESP.restart();
}

void handleCaptiveRedirect() {
  if (!wifiProvisioning.isCaptiveMode()) {
    webServer.send(404, "text/plain", "Not found");
    return;
  }
  webServer.sendHeader("Location", String("http://") + wifiProvisioning.getApIp() + "/", true);
  webServer.send(302, "text/plain", "Redirecting to captive portal");
}

void setupWebServer() {
  webServer.on("/", HTTP_GET, handleWebRoot);
  webServer.on("/api/data", HTTP_GET, handleApiData);
  webServer.on("/api/config", HTTP_GET, handleApiConfigGet);
  webServer.on("/api/config", HTTP_POST, handleApiConfigPost);
  webServer.on("/api/wifi/save", HTTP_POST, handleApiWifiSave);
  webServer.on("/api/wifi/forget", HTTP_POST, handleApiWifiForget);
  webServer.on("/api/tmep/send", HTTP_POST, handleApiTmepSend);
  webServer.on("/update", HTTP_POST, handleOtaDone, handleOtaUpload);

  webServer.on("/generate_204", HTTP_ANY, handleCaptiveRedirect);
  webServer.on("/gen_204", HTTP_ANY, handleCaptiveRedirect);
  webServer.on("/hotspot-detect.html", HTTP_ANY, handleCaptiveRedirect);
  webServer.on("/library/test/success.html", HTTP_ANY, handleCaptiveRedirect);
  webServer.on("/connecttest.txt", HTTP_ANY, handleCaptiveRedirect);
  webServer.on("/ncsi.txt", HTTP_ANY, handleCaptiveRedirect);
  webServer.on("/redirect", HTTP_ANY, handleCaptiveRedirect);

  webServer.onNotFound([]() {
    if (wifiProvisioning.isCaptiveMode()) {
      handleCaptiveRedirect();
      return;
    }
    webServer.send(404, "text/plain", "Not found");
  });

  webServer.begin();
  Serial.println("WEB: Server bezi na portu 80");
}

// =============================================
//  MQTT - HOME ASSISTANT AUTO-DISCOVERY
// =============================================

void publishHADiscovery() {
  if (!mqtt.connected()) return;

  struct HASensor {
    const char* name;
    const char* uidSuffix;
    const String* topic;
    const char* unit;
    const char* devClass;
    const char* icon;
    uint8_t precision;
  };

  const HASensor sensors[] = {
    {"Teplota",   "temp",     &topicTemp,     "°C",    "temperature",    "mdi:thermometer",   1},
    {"Vlhkost",   "humidity", &topicHumidity, "%",     "humidity",       "mdi:water-percent", 1},
    {"PM1.0",     "pm1",      &topicPm1,      "µg/m³", "pm1",            "mdi:blur",          1},
    {"PM2.5",     "pm25",     &topicPm25,     "µg/m³", "pm25",           "mdi:blur",          1},
    {"PM4.0",     "pm4",      &topicPm4,      "µg/m³", nullptr,          "mdi:blur-radial",   1},
    {"PM10",      "pm10",     &topicPm10,     "µg/m³", "pm10",           "mdi:blur-radial",   1},
    {"VOC Index", "voc",      &topicVoc,      nullptr, "aqi",            "mdi:air-filter",    0},
    {"NOx Index", "nox",      &topicNox,      nullptr, "aqi",            "mdi:molecule",      0},
    {"CO2",       "co2",      &topicCo2,      "ppm",   "carbon_dioxide", "mdi:molecule-co2",  0},
  };

  // unique_id vcetne MAC - jinak by se dve zarizeni v jedne HA prepsala.
  const String devUid = "sharp_sen66_" + deviceId;

  for (const auto& s : sensors) {
    JsonDocument doc;
    const String uid = devUid + "_" + s.uidSuffix;

    doc["name"] = s.name;
    doc["unique_id"] = uid;
    doc["state_topic"] = *s.topic;
    if (s.unit) doc["unit_of_measurement"] = s.unit;
    if (s.devClass) doc["device_class"] = s.devClass;
    if (s.icon) doc["icon"] = s.icon;
    doc["state_class"] = "measurement";  // nutne pro dlouhodobe statistiky v HA
    doc["suggested_display_precision"] = s.precision;
    doc["availability_topic"] = topicStatus;
    doc["payload_available"] = "online";
    doc["payload_not_available"] = "offline";

    JsonObject dev = doc["device"].to<JsonObject>();
    dev["identifiers"][0] = devUid;
    dev["name"] = "Sharp SEN66 Displej";
    dev["model"] = "Sharp LS027B7DH01 + SEN66";
    dev["manufacturer"] = "DIY";
    dev["sw_version"] = FW_VERSION;
    if (WiFi.status() == WL_CONNECTED) {
      dev["configuration_url"] = String("http://") + WiFi.localIP().toString();
    }

    JsonObject origin = doc["origin"].to<JsonObject>();
    origin["name"] = "sharp-sen66-mqtt-display";
    origin["sw"] = FW_VERSION;

    const String topic = appConfig.haPrefix + "/sensor/" + uid + "/config";

    String payload;
    serializeJson(doc, payload);
    if (!mqtt.publish(topic.c_str(), payload.c_str(), true)) {
      Serial.printf("HA Discovery: publish selhal (%s, %u B)\n", s.name, (unsigned)payload.length());
    }
    delay(50);
  }

  Serial.println("HA Discovery: Hotovo!");
}

// =============================================
//  MQTT - CALLBACK
// =============================================

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  message.reserve(length);
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];

  const String t(topic);
  Serial.printf("MQTT RX [%s]: %s\n", topic, message.c_str());

  // --- HA nastartoval (birth message): poslat discovery znovu ---
  if (t == topicHaStatus) {
    if (message == "online") {
      Serial.println("MQTT: HA birth message, posilam discovery znovu");
      delay(random(200, 1500));  // rozprostreni zateze dle doporuceni HA
      publishHADiscovery();
    }
    return;
  }

  // --- TEXT ---
  if (t == topicText) {
    overrideText = message;
    overrideTextSize = 2;
    overrideX = 10;
    overrideY = 10;
    displayOverride = true;
    displayOverrideUntil = millis() + 30000UL;
    drawCustomTextScreen();
    return;
  }

  // --- CLEAR ---
  if (t == topicClear) {
    displayOverride = false;
    clearCanvas();
    display.refresh();
    Serial.println("Display cleared");
    return;
  }

  // --- COMMAND (JSON) ---
  if (t != topicCommand) return;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, message);
  if (err) {
    Serial.printf("JSON parse error: %s\n", err.c_str());
    return;
  }

  // {"text":"Hello","x":10,"y":50,"size":3,"duration":60}
  if (doc["text"].is<const char*>()) {
    overrideText = doc["text"].as<String>();
    overrideX = doc["x"] | 10;
    overrideY = doc["y"] | 10;
    overrideTextSize = constrain((int)(doc["size"] | 2), 1, 8);
    const long duration = constrain((long)(doc["duration"] | 30), 1L, 86400L);
    displayOverride = true;
    displayOverrideUntil = millis() + (unsigned long)duration * 1000UL;
    drawCustomTextScreen();
  }

  // {"line":{"x1":0,"y1":120,"x2":399,"y2":120}}
  if (doc["line"].is<JsonObject>()) {
    JsonObject line = doc["line"];
    display.drawLine(line["x1"] | 0, line["y1"] | 0,
                     line["x2"] | (DISPLAY_WIDTH - 1), line["y2"] | 0, inkColor());
    display.refresh();
  }

  // {"rect":{"x":10,"y":10,"w":100,"h":50,"fill":false}}
  if (doc["rect"].is<JsonObject>()) {
    JsonObject rect = doc["rect"];
    const int x = rect["x"] | 0;
    const int y = rect["y"] | 0;
    const int w = rect["w"] | 50;
    const int h = rect["h"] | 30;
    const bool fill = rect["fill"] | false;
    if (fill) {
      display.fillRect(x, y, w, h, inkColor());
    } else {
      display.drawRect(x, y, w, h, inkColor());
    }
    display.refresh();
  }

  // {"invert":true} - softwarova inverze (Sharp LCD nema HW inverzi)
  if (doc["invert"].is<bool>()) {
    appConfig.displayInvertRequested = doc["invert"].as<bool>();
    Serial.printf("Display: inverze %s (docasne, neuklada se do NVS)\n",
                  appConfig.displayInvertRequested ? "zapnuta" : "vypnuta");
    if (displayOverride) {
      drawCustomTextScreen();
    } else {
      drawSensorScreen();
    }
  }

  // {"dashboard":true}
  if (doc["dashboard"].is<bool>()) {
    displayOverride = false;
    drawSensorScreen();
  }
}

// =============================================
//  MQTT - PUBLISH SENSOR DATA
// =============================================

void publishSensorData() {
  if (!mqtt.connected() || !sensorData.valid) return;
  if (!sensorWarmupDone()) {
    Serial.println("MQTT: warmup delay aktivni, publikace preskocena");
    return;
  }

  char buf[16];

  snprintf(buf, sizeof(buf), "%.1f", sensorData.temperature);
  mqtt.publish(topicTemp.c_str(), buf, true);

  snprintf(buf, sizeof(buf), "%.1f", sensorData.humidity);
  mqtt.publish(topicHumidity.c_str(), buf, true);

  snprintf(buf, sizeof(buf), "%.1f", sensorData.pm1);
  mqtt.publish(topicPm1.c_str(), buf, true);

  snprintf(buf, sizeof(buf), "%.1f", sensorData.pm25);
  mqtt.publish(topicPm25.c_str(), buf, true);

  snprintf(buf, sizeof(buf), "%.1f", sensorData.pm4);
  mqtt.publish(topicPm4.c_str(), buf, true);

  snprintf(buf, sizeof(buf), "%.1f", sensorData.pm10);
  mqtt.publish(topicPm10.c_str(), buf, true);

  snprintf(buf, sizeof(buf), "%.0f", sensorData.voc);
  mqtt.publish(topicVoc.c_str(), buf, true);

  snprintf(buf, sizeof(buf), "%.0f", sensorData.nox);
  mqtt.publish(topicNox.c_str(), buf, true);

  if (sensorData.co2Valid) {
    snprintf(buf, sizeof(buf), "%u", sensorData.co2);
    mqtt.publish(topicCo2.c_str(), buf, true);
  }

  JsonDocument doc;
  doc["temperature"] = round(sensorData.temperature * 10) / 10.0;
  doc["humidity"]    = round(sensorData.humidity * 10) / 10.0;
  doc["pm1"]  = round(sensorData.pm1 * 10) / 10.0;
  doc["pm25"] = round(sensorData.pm25 * 10) / 10.0;
  doc["pm4"]  = round(sensorData.pm4 * 10) / 10.0;
  doc["pm10"] = round(sensorData.pm10 * 10) / 10.0;
  doc["voc"]  = round(sensorData.voc);
  doc["nox"]  = round(sensorData.nox);
  if (sensorData.co2Valid) doc["co2"] = sensorData.co2;
  doc["quality"] = getAirQuality(sensorData.pm25);
  doc["rssi"] = WiFi.RSSI();
  doc["uptime"] = millis() / 1000;

  String jsonBuf;
  serializeJson(doc, jsonBuf);
  mqtt.publish(topicSensor.c_str(), jsonBuf.c_str(), true);

  Serial.printf("MQTT: publikovano, JSON: %s\n", jsonBuf.c_str());
}

// =============================================
//  MQTT - AVAILABILITY A UKLID
// =============================================

// "online" jen kdyz data ze senzoru nejsou zamrzla - jinak by HA dal ukazoval
// posledni hodnotu, jako by byla aktualni.
void publishAvailability() {
  if (!mqtt.connected()) return;
  mqtt.publish(topicStatus.c_str(), sensorStale ? "offline" : "online", true);
}

// FW <= 2.0.0 publikoval discovery pod unique_id "sen66_*" (bez MAC) do pevneho
// prefixu "homeassistant". Ty retained zpravy na brokeru zustaly a mirily na
// stejne state topicy jako nove, takze HA mel kazdou entitu dvakrat. Prazdna
// retained zprava je v HA smaze.
void cleanupLegacyDiscovery() {
  static const char* const legacyIds[] = {
    "sen66_temp", "sen66_humidity", "sen66_pm1", "sen66_pm25", "sen66_pm4",
    "sen66_pm10", "sen66_voc", "sen66_nox", "sen66_co2",
  };
  for (const char* id : legacyIds) {
    const String topic = String("homeassistant/sensor/") + id + "/config";
    mqtt.publish(topic.c_str(), "", true);
  }
  Serial.println("HA Discovery: stare entity z FW 2.0.0 odstraneny");
}

// =============================================
//  MQTT - CONNECT
// =============================================

bool reconnectMQTT() {
  if (appConfig.mqttServer.length() == 0) return false;

  Serial.print("MQTT: Pripojuji...");

  // Client ID musi byt na brokeru unikatni - pridavame MAC suffix.
  const String clientId = appConfig.mqttClientId + "-" + deviceId;

  const char* user = appConfig.mqttUser.length() ? appConfig.mqttUser.c_str() : nullptr;
  const char* pass = appConfig.mqttPassword.length() ? appConfig.mqttPassword.c_str() : nullptr;

  if (!mqtt.connect(clientId.c_str(), user, pass,
                    topicStatus.c_str(), 0, true, "offline")) {
    Serial.printf("CHYBA rc=%d\n", mqtt.state());
    return false;
  }

  Serial.println("OK!");

  publishAvailability();

  mqtt.subscribe(topicText.c_str());
  mqtt.subscribe(topicClear.c_str());
  mqtt.subscribe(topicCommand.c_str());
  mqtt.subscribe(topicHaStatus.c_str());

  if (!legacyDiscoveryCleaned) {
    cleanupLegacyDiscovery();
    legacyDiscoveryCleaned = true;
  }
  publishHADiscovery();
  return true;
}

// =============================================
//  SETUP
// =============================================

void setup() {
  Serial.begin(115200);
  delay(2000);  // ESP32-C3 potrebuje cas na USB Serial

  Serial.println("\n========================================");
  Serial.println("  Sharp LCD + SEN66 + MQTT v" FW_VERSION);
  Serial.println("========================================\n");

  const bool configLoaded = loadConfig(appConfig);
  Serial.printf("CFG: load %s\n", configLoaded ? "OK" : "FAILED - defaults");

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char idBuf[7];
  snprintf(idBuf, sizeof(idBuf), "%02x%02x%02x", mac[3], mac[4], mac[5]);
  deviceId = String(idBuf);

  buildTopics();

  Serial.printf("CFG: device id=%s, base topic=%s, HA prefix=%s\n",
                deviceId.c_str(), appConfig.mqttBaseTopic.c_str(), appConfig.haPrefix.c_str());
  Serial.printf("CFG: MQTT %s:%d, publish=%lu ms, TMEP=%lu ms, warmup=%lu ms\n",
                appConfig.mqttServer.length() ? appConfig.mqttServer.c_str() : "(nenastaveno)",
                appConfig.mqttPort, appConfig.mqttPublishInterval,
                appConfig.tmepRequestInterval, appConfig.mqttWarmupDelay);
  Serial.printf("CFG: TMEP domena: %s\n",
                appConfig.tmepDomain.length() ? appConfig.tmepDomain.c_str() : "(nenastaveno)");
  Serial.printf("CFG: temperature offset=%.2f, web auth=%s\n",
                appConfig.temperatureOffset,
                appConfig.webPassword.length() ? "zapnuta" : "vypnuta");

  // 1. Displej
  Serial.println("Display: Inicializace...");
  display.begin();
  applyDisplaySettings();
  clearCanvas();
  drawSplashScreen();
  Serial.println("Display: OK!");

  // 2. WiFi provisioning (STA / AP captive)
  wifiProvisioning.begin(&appConfig, 20000UL);

  // 3. MQTT
  mqtt.setServer(appConfig.mqttServer.c_str(), appConfig.mqttPort);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(2048);  // HA discovery payload s device+origin ma pres 500 B
  mqtt.setKeepAlive(30);

  if (wifiProvisioning.getState() == WIFI_STA_CONNECTED) {
    reconnectMQTT();
  }

  setupWebServer();

  // 4. SEN66
  Wire.begin(PIN_SDA, PIN_SCL);
  initSEN66();
  lastSen66InitAttempt = millis();

  // 5. Splash na 2 sekundy
  delay(2000);

  Serial.println("\n=== SETUP HOTOV ===\n");
}

// =============================================
//  LOOP
// =============================================

void loop() {
  const unsigned long now = millis();

  wifiProvisioning.process();
  webServer.handleClient();

  if (wifiProvisioning.getState() == WIFI_STA_CONNECTED && !mqtt.connected()) {
    if (now - lastMqttReconnect > MQTT_RECONNECT_INTERVAL) {
      lastMqttReconnect = now;
      reconnectMQTT();
    }
  }

  if (mqtt.connected()) {
    mqtt.loop();
  }

  // Senzor, ktery neprosel inicializaci nebo opakovane selhava, zkousime znovu.
  if (!sen66Ready && now - lastSen66InitAttempt >= SEN66_REINIT_INTERVAL) {
    lastSen66InitAttempt = now;
    initSEN66();
  }

  if (now - lastSensorRead > SENSOR_READ_INTERVAL) {
    lastSensorRead = now;
    readSEN66();
  }

  // Zamrzla data: dlouho zadny platny vzorek => neplatne. Displej ukaze
  // "Cekam na data", MQTT/TMEP prestanou posilat stare hodnoty a HA dostane
  // availability "offline". Po obnoveni dat se znovu ceka warmup.
  if (sensorData.valid && millis() - lastValidSensorAt > SENSOR_STALE_TIMEOUT) {
    sensorData.valid = false;
    sensorData.co2Valid = false;
    firstValidSensorAt = 0;
    sensorStale = true;
    Serial.println("SEN66: zadna platna data, hodnoty oznaceny jako neplatne");
    publishAvailability();
  }

  if (now - lastMqttPublish > appConfig.mqttPublishInterval) {
    lastMqttPublish = now;
    publishSensorData();
  }

  if (now - lastTmepRequest > appConfig.tmepRequestInterval) {
    lastTmepRequest = now;
    sendTmepRequest(false);
  }

  // Pozor: znamenkovy rozdil, aby to prezilo preteceni millis() po ~49,7 dne.
  if (displayOverride && (long)(now - displayOverrideUntil) >= 0) {
    displayOverride = false;
    Serial.println("Display: Override expired, zpet na dashboard");
  }

  if (now - lastDisplayRefresh > appConfig.displayRefreshInterval) {
    lastDisplayRefresh = now;
    if (!displayOverride) {
      drawSensorScreen();
    }
  }

  delay(10);
}
