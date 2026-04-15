#include "WifiManagerWrapper.h"
#include "core/CoreConfig.h"
#include <WiFi.h>

void WifiManagerWrapper::handleWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED || event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    _authExpireCount = 0;
    _portalForcedDueToAuthExpire = false;
    return;
  }
  if (event != ARDUINO_EVENT_WIFI_STA_DISCONNECTED) return;

  if (info.wifi_sta_disconnected.reason == WIFI_REASON_AUTH_EXPIRE) {
    if (_authExpireCount < 255) ++_authExpireCount;
    if (_authExpireCount >= 5 && !_portalForcedDueToAuthExpire) {
      Serial.println("[WiFi] AUTH_EXPIRE repeated; clearing saved STA creds and reopening config portal");
      _wm.resetSettings();
      _wm.startConfigPortal(_apName, _apPass);
      _portalForcedDueToAuthExpire = true;
    }
  } else {
    _authExpireCount = 0;
  }
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
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  _wm.setDebugOutput(true);
  _wm.setConnectRetries(8);
  _wm.setConfigPortalBlocking(false);
  _wm.setConfigPortalTimeout(portalTimeoutSec);
  _wifiEventHandler = WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
    handleWifiEvent(event, info);
  });
  _wm.autoConnect(_apName, _apPass);
  _started = true;
}

void WifiManagerWrapper::update() {
  if (_started) {
    _wm.process();
    if (WiFi.status() != WL_CONNECTED && millis() - _lastReconnectAttemptMs > 10000) {
      _lastReconnectAttemptMs = millis();
      Serial.println("[WiFi] reconnect attempt");
      WiFi.reconnect();
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
