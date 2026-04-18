#include "WifiManagerWrapper.h"
#include "core/CoreConfig.h"
#include <WiFi.h>

namespace {
#if defined(DEV_FORCE_WIFI_CREDENTIALS)
constexpr char kForcedSsid[] = "project6";
constexpr char kForcedPass[] = "sIlver@99";
constexpr bool kUseForcedCredentials = true;
#else
constexpr char kForcedSsid[] = "";
constexpr char kForcedPass[] = "";
constexpr bool kUseForcedCredentials = false;
#endif
}

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
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.persistent(false);
  _wm.setDebugOutput(true);
  _wm.setConnectRetries(8);
  _wm.setConfigPortalBlocking(false);
  _wm.setConfigPortalTimeout(portalTimeoutSec);

  if (kUseForcedCredentials) {
    Serial.printf("[WiFi] attempting forced credentials SSID=%s\n", kForcedSsid);
    WiFi.disconnect(false, false);
    delay(100);
    WiFi.begin(kForcedSsid, kForcedPass);
    const uint32_t forcedStart = millis();
    while (millis() - forcedStart < 15000) {
      if (WiFi.status() == WL_CONNECTED) break;
      delay(50);
    }

    if (WiFi.status() == WL_CONNECTED) {
      _started = true;
      return;
    }
  }

  WiFi.disconnect(false, false);
  delay(100);
  _wm.autoConnect(_apName, _apPass);
  _started = true;
}

void WifiManagerWrapper::update() {
  if (_started) {
    _wm.process();
    if (kUseForcedCredentials &&
        WiFi.status() != WL_CONNECTED &&
        millis() - _lastReconnectAttemptMs > 10000) {
      _lastReconnectAttemptMs = millis();
      Serial.println("[WiFi] reconnect attempt using forced credentials");
      WiFi.disconnect(false, false);
      delay(50);
      WiFi.begin(kForcedSsid, kForcedPass);
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
  WiFi.disconnect(false, true);
}
