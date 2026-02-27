#pragma once

#include <Arduino.h>

struct AppConfig {
  String wifiSsid = "";
  String wifiPassword = "";

  String mqttServer = "192.168.0.X";
  int mqttPort = 1883;
  String mqttUser = "";
  String mqttPassword = "";
  String mqttClientId = "sharp";

  String tmepDomain = "";
  String tmepParams = "tempV=*TEMP*&humV=*HUM*&pm1=*PM1*&pm2=*PM2*&pm4=*PM4*&pm10=*PM10*&voc=*VOC*&nox=*NOX*&co2=*CO2*";

  unsigned long mqttPublishInterval = 10000;
  unsigned long tmepRequestInterval = 60000;
  unsigned long displayRefreshInterval = 2000;
  unsigned long mqttWarmupDelay = 60000;

  String tmepBaseUrl = "";

  uint8_t displayRotation = 2;
  bool displayInvertRequested = false;
};

bool loadConfig(AppConfig& config);
bool saveConfig(const AppConfig& config);
bool validateConfig(const AppConfig& config);
