#pragma once

#include <Adafruit_GFX.h>
#include <stdint.h>

namespace dashboard {
struct Data {
  float temperature, humidity;
  float pm[4];
  float voc, nox;
  uint16_t co2;
  bool valid, co2Valid, sensorReady, stale, mqttConnected;
  const char* wifi;
  const char* tmep;
  uint32_t uptimeSeconds;
};

// The bands also drive the existing MQTT quality labels; no threshold changes.
uint8_t airQualityBand(float pm25);
void draw(Adafruit_GFX& display, const Data& data, uint16_t ink, uint16_t paper);
void message(Adafruit_GFX& display, const char* title, const char* const* lines,
             uint8_t count, uint16_t ink, uint16_t paper);
}  // namespace dashboard
