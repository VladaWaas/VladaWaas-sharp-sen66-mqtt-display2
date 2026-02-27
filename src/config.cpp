#include "config.h"

#include <Preferences.h>

namespace {
constexpr const char* NS = "appcfg";

void sanitize(AppConfig& cfg) {
  if (cfg.mqttPort < 1 || cfg.mqttPort > 65535) cfg.mqttPort = 1883;
  if (cfg.displayRotation > 3) cfg.displayRotation = 2;
  if (cfg.displayRefreshInterval < 500) cfg.displayRefreshInterval = 2000;
  if (cfg.mqttPublishInterval < 1000) cfg.mqttPublishInterval = 10000;
  if (cfg.tmepRequestInterval < 1000) cfg.tmepRequestInterval = 60000;
  if (cfg.mqttWarmupDelay < 1000) cfg.mqttWarmupDelay = 60000;
}
}  // namespace

bool validateConfig(const AppConfig& cfg) {
  if (cfg.wifiSsid.length() == 0) return false;
  if (cfg.mqttServer.length() == 0) return false;
  if (cfg.mqttPort < 1 || cfg.mqttPort > 65535) return false;
  if (cfg.displayRotation > 3) return false;
  if (cfg.displayRefreshInterval < 500) return false;
  if (cfg.mqttPublishInterval < 1000) return false;
  if (cfg.tmepRequestInterval < 1000) return false;
  if (cfg.mqttWarmupDelay < 1000) return false;
  return true;
}

bool loadConfig(AppConfig& config) {
  Preferences pref;
  if (!pref.begin(NS, true)) return false;

  config.wifiSsid = pref.getString("wifi_ssid", config.wifiSsid);
  config.wifiPassword = pref.getString("wifi_pass", config.wifiPassword);

  config.mqttServer = pref.getString("mqtt_server", config.mqttServer);
  config.mqttPort = pref.getInt("mqtt_port", config.mqttPort);
  config.mqttUser = pref.getString("mqtt_user", config.mqttUser);
  config.mqttPassword = pref.getString("mqtt_pass", config.mqttPassword);
  config.mqttClientId = pref.getString("mqtt_client", config.mqttClientId);

  config.tmepDomain = pref.getString("tmep_domain", config.tmepDomain);
  config.tmepParams = pref.getString("tmep_params", config.tmepParams);

  config.mqttPublishInterval = pref.getULong("mqtt_pub_ms", config.mqttPublishInterval);
  config.tmepRequestInterval = pref.getULong("tmep_req_ms", config.tmepRequestInterval);
  config.displayRefreshInterval = pref.getULong("disp_ref_ms", config.displayRefreshInterval);
  config.mqttWarmupDelay = pref.getULong("mqtt_warmup", config.mqttWarmupDelay);

  config.tmepBaseUrl = pref.getString("tmep_base", config.tmepBaseUrl);

  config.displayRotation = pref.getUChar("disp_rot", config.displayRotation);
  config.displayInvertRequested = pref.getBool("disp_inv", config.displayInvertRequested);

  pref.end();
  sanitize(config);
  return true;
}

bool saveConfig(const AppConfig& config) {
  if (!validateConfig(config)) return false;

  Preferences pref;
  if (!pref.begin(NS, false)) return false;

  pref.putString("wifi_ssid", config.wifiSsid);
  pref.putString("wifi_pass", config.wifiPassword);

  pref.putString("mqtt_server", config.mqttServer);
  pref.putInt("mqtt_port", config.mqttPort);
  pref.putString("mqtt_user", config.mqttUser);
  pref.putString("mqtt_pass", config.mqttPassword);
  pref.putString("mqtt_client", config.mqttClientId);

  pref.putString("tmep_domain", config.tmepDomain);
  pref.putString("tmep_params", config.tmepParams);

  pref.putULong("mqtt_pub_ms", config.mqttPublishInterval);
  pref.putULong("tmep_req_ms", config.tmepRequestInterval);
  pref.putULong("disp_ref_ms", config.displayRefreshInterval);
  pref.putULong("mqtt_warmup", config.mqttWarmupDelay);

  pref.putString("tmep_base", config.tmepBaseUrl);

  pref.putUChar("disp_rot", config.displayRotation);
  pref.putBool("disp_inv", config.displayInvertRequested);

  pref.end();
  return true;
}
