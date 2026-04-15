#include "WifiManagerWrapper.h"
#include "core/CoreConfig.h"
#include <WiFi.h>

void WifiManagerWrapper::loadSavedCredentials() {
  if (_credentialsLoaded) return;
  _wifiPrefs.begin("wifi-cred", false);
  _savedSsid = _wifiPrefs.getString("ssid", "");
  _savedPass = _wifiPrefs.getString("pass", "");
  _haveSavedCredentials = !_savedSsid.isEmpty();
  _credentialsLoaded = true;
}

void WifiManagerWrapper::saveCurrentCredentials() {
  loadSavedCredentials();
  const String currentSsid = WiFi.SSID();
  if (currentSsid.isEmpty() || currentSsid == _lastPersistedSsid) return;
  const String currentPass = WiFi.psk();
  _wifiPrefs.putString("ssid", currentSsid);
  _wifiPrefs.putString("pass", currentPass);
  _savedSsid = currentSsid;
  _savedPass = currentPass;
  _lastPersistedSsid = currentSsid;
  _haveSavedCredentials = true;
  Serial.printf("[WiFi] persisted credentials for SSID=%s\n", currentSsid.c_str());
}

bool WifiManagerWrapper::connectWithSavedCredentials(uint32_t timeoutMs) {
  loadSavedCredentials();
  if (!_haveSavedCredentials) return false;

  Serial.printf("[WiFi] attempting saved credentials SSID=%s\n", _savedSsid.c_str());
  WiFi.begin(_savedSsid.c_str(), _savedPass.c_str());
  const uint32_t started = millis();
  while (millis() - started < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      saveCurrentCredentials();
      return true;
    }
    delay(50);
  }
  return WiFi.status() == WL_CONNECTED;
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
  loadSavedCredentials();
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  _wm.setDebugOutput(true);
  _wm.setConnectRetries(8);
  _wm.setConfigPortalBlocking(false);
  _wm.setConfigPortalTimeout(portalTimeoutSec);
  const bool restored = connectWithSavedCredentials();
  if (!restored) _wm.autoConnect(_apName, _apPass);
  _started = true;
}

void WifiManagerWrapper::update() {
  if (_started) {
    _wm.process();
    if (WiFi.status() == WL_CONNECTED) saveCurrentCredentials();
    if (WiFi.status() != WL_CONNECTED && millis() - _lastReconnectAttemptMs > 10000) {
      _lastReconnectAttemptMs = millis();
      if (_haveSavedCredentials) {
        Serial.println("[WiFi] reconnect attempt using persisted credentials");
        WiFi.begin(_savedSsid.c_str(), _savedPass.c_str());
      } else if (WiFi.SSID().length() > 0) {
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
  loadSavedCredentials();
  _wifiPrefs.remove("ssid");
  _wifiPrefs.remove("pass");
  _savedSsid = "";
  _savedPass = "";
  _lastPersistedSsid = "";
  _haveSavedCredentials = false;
}
