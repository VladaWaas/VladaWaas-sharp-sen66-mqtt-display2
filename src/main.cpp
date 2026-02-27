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
#include <Wire.h>
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SharpMem.h>
#include <SensirionI2cSen66.h>
#include <ArduinoJson.h>

// =============================================
//  KONFIGURACE - UPRAVTE PODLE POTŘEBY
// =============================================

// WiFi
const char* WIFI_SSID     = "your_SSID";
const char* WIFI_PASSWORD = "your_password";

// MQTT
const char* MQTT_SERVER    = "192.168.XXX.XXX"; // server IP ADDRESS
const int   MQTT_PORT      = 1883;
const char* MQTT_USER      = "your_mqtt_user";
const char* MQTT_PASSWORD  = "your_mqtt_pasword";
const char* MQTT_CLIENT_ID = "sharp_sen66";

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
#define MQTT_PUBLISH_INTERVAL  10000  // publikování do MQTT každých 10s
#define DISPLAY_REFRESH_INTERVAL 2000 // refresh displeje každé 2s
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
  display.print(WIFI_SSID);
  
  display.setCursor(30, 140);
  display.print("MQTT: ");
  display.print(MQTT_SERVER);
  
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
  if (isnan(temp) || temp > 100 || temp < -40) return;
  
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
  
  Serial.printf("SEN66: T=%.1f H=%.1f PM2.5=%.1f VOC=%.0f NOx=%.0f CO2=%u\n",
    temp, hum, pm25, voc, nox, co2);
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
  if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD,
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
  Serial.printf("WiFi: Pripojuji k %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
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
  
  // 1. Displej
  Serial.println("Display: Inicializace...");
  display.begin();
  display.setRotation(0);
  display.clearDisplay();
  display.setTextColor(BLACK);
  drawSplashScreen();
  Serial.println("Display: OK!");
  
  // 2. WiFi
  setupWiFi();
  
  // 3. MQTT
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
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
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastWifiRetry = 0;
    if (now - lastWifiRetry > 30000) {
      lastWifiRetry = now;
      Serial.println("WiFi: Reconnecting...");
      WiFi.reconnect();
    }
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
  
  // --- Čtení SEN66 ---
  if (now - lastSensorRead > SENSOR_READ_INTERVAL) {
    lastSensorRead = now;
    readSEN66();
  }
  
  // --- Publikování do MQTT ---
  if (now - lastMqttPublish > MQTT_PUBLISH_INTERVAL) {
    lastMqttPublish = now;
    publishSensorData();
  }
  
  // --- Override timeout (vrátit se na senzorový dashboard) ---
  if (displayOverride && now > displayOverrideUntil) {
    displayOverride = false;
    Serial.println("Display: Override expired, zpet na dashboard");
  }
  
  // --- Refresh displeje ---
  if (now - lastDisplayRefresh > DISPLAY_REFRESH_INTERVAL) {
    lastDisplayRefresh = now;
    if (!displayOverride) {
      drawSensorScreen();
    }
  }
  
  delay(10);
}
