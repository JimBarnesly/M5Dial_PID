#pragma once

#include <cstring>

#include "AppState.h"

namespace TestingMode {
constexpr char WIFI_SSID[] = "project6";
constexpr char WIFI_PASSWORD[] = "sIlver@99";
constexpr char MQTT_HOST[] = "10.42.0.1";
constexpr uint16_t MQTT_PORT = 1883;

inline bool enabled(const PersistentConfig& cfg) {
  return cfg.testingModeEnabled;
}

inline bool alarmsForcedOff(const PersistentConfig& cfg) {
  return enabled(cfg);
}

inline const char* wifiSsid() {
  return WIFI_SSID;
}

inline const char* wifiPassword() {
  return WIFI_PASSWORD;
}

inline const char* mqttHost(const PersistentConfig& cfg) {
  return enabled(cfg) ? MQTT_HOST : cfg.mqttHost;
}

inline uint16_t mqttPort(const PersistentConfig& cfg) {
  return enabled(cfg) ? MQTT_PORT : cfg.mqttPort;
}

inline IPAddress mqttIp() {
  return IPAddress(10, 42, 0, 1);
}

inline bool mqttTlsEnabled(const PersistentConfig& cfg) {
  return enabled(cfg) ? false : cfg.mqttUseTls;
}

inline bool mqttUseAuth(const PersistentConfig& cfg) {
  return !enabled(cfg) && std::strlen(cfg.mqttUser) > 0;
}
}
