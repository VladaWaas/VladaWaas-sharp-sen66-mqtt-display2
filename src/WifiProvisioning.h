#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>

#include "config.h"

enum WifiModeState {
  WIFI_STA_CONNECTING = 0,
  WIFI_STA_CONNECTED,
  WIFI_AP_CAPTIVE,
};

class WifiProvisioning {
 public:
  void begin(AppConfig* config, unsigned long connectTimeoutMs = 20000UL);
  void process();

  bool saveCredentialsAndConnect(const String& ssid, const String& password, String& statusMsg);
  bool forgetCredentials();

  WifiModeState getState() const { return state_; }
  bool isCaptiveMode() const { return state_ == WIFI_AP_CAPTIVE; }
  String getStateText() const;
  String getApSsid() const { return apSsid_; }
  String getApIp() const { return apIp_.toString(); }

 private:
  bool connectStaBlocking(unsigned long timeoutMs);
  void startCaptiveMode();
  void stopCaptiveMode();
  bool hasStoredCredentials() const;

  AppConfig* config_ = nullptr;
  DNSServer dnsServer_;
  WifiModeState state_ = WIFI_STA_CONNECTING;

  bool captiveRunning_ = false;
  IPAddress apIp_ = IPAddress(192, 168, 4, 1);
  String apSsid_;
  unsigned long connectTimeoutMs_ = 20000UL;

  unsigned long staConnectStartedAt_ = 0;
  unsigned long lastReconnectAttemptAt_ = 0;
};
