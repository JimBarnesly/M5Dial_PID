#include "WifiManagerWrapper.h"
#include "core/CoreConfig.h"
#include <WiFi.h>

void WifiManagerWrapper::loadSavedCredentials() {
  if (_credentialsLoaded) return;
  _wifiPrefs.begin("wifi-cred", false);
  _savedSsid = _wifiPrefs.getString("ssid", "");
  _savedPass = _wifiPrefs.getString("pass", "");

  // If an SSID exists but the password is blank, assume a prior bad write and
  // do not use the persisted pair. Fall back to the SDK-saved station config.
  if (!_savedSsid.isEmpty() && _savedPass.isEmpty()) {
    Serial.printf("[WiFi] ignoring persisted credentials for SSID=%s because password is blank\n",
                  _savedSsid.c_str());
    _wifiPrefs.remove("ssid");
    _wifiPrefs.remove("pass");
    _savedSsid = "";
    _savedPass = "";
  }

  _haveSavedCredentials = !_savedSsid.isEmpty() && !_savedPass.isEmpty();
  _credentialsLoaded = true;
}

void WifiManagerWrapper::saveCurrentCredentials() {
  // Intentionally disabled.
  // Do not persist credentials from WiFi runtime state; WiFi.psk() is not a
  // reliable source after reconnect/restore and can overwrite the stored
  // password with an empty string.
}

bool WifiManagerWrapper::connectWithSavedCredentials(uint32_t timeoutMs) {
  loadSavedCredentials();
  if (!_haveSavedCredentials) return false;

  Serial.printf("[WiFi] attempting persisted credentials SSID=%s\n", _savedSsid.c_str());
  WiFi.disconnect(true, false);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  delay(100);
  WiFi.begin(_savedSsid.c_str(), _savedPass.c_str());

  const uint32_t started = millis();
  while (millis() - started < timeoutMs) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      Serial.println("[WiFi] persisted credential connect success");
      return true;
    }
    delay(50);
  }

  Serial.printf("[WiFi] persisted credential connect failed, status=%d\n",
                static_cast<int>(WiFi.status()));
  return WiFi.status() == WL_CONNECTED;
}

bool WifiManagerWrapper::connectWithSdkSavedCredentials(uint32_t timeoutMs) {
  Serial.println("[WiFi] attempting SDK-saved credentials");
  WiFi.disconnect(true, false);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  delay(100);
  WiFi.begin();

  const uint32_t started = millis();
  while (millis() - started < timeoutMs) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      Serial.println("[WiFi] SDK-saved credential connect success");
      return true;
    }
    delay(50);
  }

  Serial.printf("[WiFi] SDK-saved credential connect failed, status=%d\n",
                static_cast<int>(WiFi.status()));
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
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  _wm.setDebugOutput(true);
  _wm.setConnectRetries(8);
  _wm.setConfigPortalBlocking(false);
  _wm.setConfigPortalTimeout(portalTimeoutSec);

  bool restored = connectWithSavedCredentials();
  if (!restored) {
    restored = connectWithSdkSavedCredentials();
  }
  if (!restored) {
    _wm.autoConnect(_apName, _apPass);
  }
  _started = true;
}

void WifiManagerWrapper::update() {
  if (!_started) return;

  _wm.process();

  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    return;
  }

  // Avoid starting a new connection attempt while the station is already busy.
  if (st == WL_IDLE_STATUS) {
    return;
  }

  if (millis() - _lastReconnectAttemptMs <= 10000) {
    return;
  }

  _lastReconnectAttemptMs = millis();
  Serial.printf("[WiFi] reconnect attempt, status=%d\n", static_cast<int>(st));

  WiFi.disconnect(false, false);
  delay(100);

  if (_haveSavedCredentials) {
    Serial.println("[WiFi] reconnect attempt using persisted credentials");
    WiFi.begin(_savedSsid.c_str(), _savedPass.c_str());
  } else if (WiFi.SSID().length() > 0) {
    Serial.println("[WiFi] reconnect attempt using SDK-saved credentials");
    WiFi.begin();
  } else {
    Serial.println("[WiFi] reconnect attempt");
    WiFi.reconnect();
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
