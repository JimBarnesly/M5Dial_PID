#include "WifiManagerWrapper.h"
#include "core/CoreConfig.h"
#include <WiFi.h>

void WifiManagerWrapper::buildPortalCredentials() {
  uint64_t efuseMac = ESP.getEfuseMac();
  uint32_t suffix = static_cast<uint32_t>(efuseMac & 0xFFFFFFULL);
  snprintf(_apName, sizeof(_apName), "%s%06lX", CoreConfig::WIFI_AP_NAME_PREFIX, static_cast<unsigned long>(suffix));
  snprintf(_apPass,
           sizeof(_apPass),
           "%s%06lX!",
           CoreConfig::WIFI_AP_PASS_PREFIX,
           static_cast<unsigned long>(suffix));
}

void WifiManagerWrapper::begin(uint16_t portalTimeoutSec, const char* defaultMqttHost, uint16_t defaultMqttPort) {
  (void)defaultMqttHost;
  (void)defaultMqttPort;

  buildPortalCredentials();
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  _wm.setDebugOutput(true);
  _wm.setConnectRetries(8);
  _wm.setConfigPortalBlocking(false);
  _wm.setConfigPortalTimeout(portalTimeoutSec);
  _wm.autoConnect(_apName, _apPass);
  _started = true;
}

void WifiManagerWrapper::update() {
  if (_started) {
    _wm.process();
    if (WiFi.status() != WL_CONNECTED && millis() - _lastReconnectAttemptMs > 10000) {
      _lastReconnectAttemptMs = millis();
      if (WiFi.SSID().length() > 0) {
        Serial.println("[WiFi] reconnect attempt using saved credentials");
        WiFi.begin();
      } else {
        Serial.println("[WiFi] reconnect attempt");
        WiFi.reconnect();
      }
    }
  }
}

bool WifiManagerWrapper::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

const char* WifiManagerWrapper::getPortalApName() const {
  return _apName;
}

const char* WifiManagerWrapper::getPortalApPassword() const {
  return _apPass;
}

void WifiManagerWrapper::resetSettings() {
  _wm.resetSettings();
}
