#include "WifiManagerWrapper.h"
#include "core/CoreConfig.h"
#include <WiFi.h>

void WifiManagerWrapper::loadSavedCredentials() {
  if (_credentialsLoaded) return;

  // Legacy app-managed credential storage is intentionally retired.
  // Earlier builds could overwrite the stored password with a blank value,
  // which then caused AUTH_EXPIRE loops after a reboot. WiFiManager already
  // persists station credentials in the ESP32 SDK/NVS, so use that single
  // source of truth and clear any leftover app-managed values.
  _wifiPrefs.begin("wifi-cred", false);
  if (_wifiPrefs.isKey("ssid") || _wifiPrefs.isKey("pass")) {
    Serial.println("[WiFi] clearing legacy app-managed credentials");
    _wifiPrefs.remove("ssid");
    _wifiPrefs.remove("pass");
  }

  _savedSsid = "";
  _savedPass = "";
  _haveSavedCredentials = false;
  _credentialsLoaded = true;
}

void WifiManagerWrapper::saveCurrentCredentials() {
  // Intentionally disabled.
  // Do not persist credentials from WiFi runtime state; WiFi.psk() is not a
  // reliable source after reconnect/restore and can overwrite the stored
  // password with an empty string.
}

bool WifiManagerWrapper::connectWithSavedCredentials(uint32_t timeoutMs) {
  (void)timeoutMs;
  loadSavedCredentials();
  return false;
}

bool WifiManagerWrapper::connectWithSdkSavedCredentials(uint32_t timeoutMs) {
  Serial.println("[WiFi] attempting SDK-saved credentials");
  WiFi.disconnect(false, false);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  delay(300);
  WiFi.begin();

  const uint32_t started = millis();
  while (millis() - started < timeoutMs) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      Serial.println("[WiFi] SDK-saved credential connect success");
      return true;
    }
    if (st == WL_CONNECT_FAILED) {
      break;
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
  WiFi.persistent(true);
  _wm.setDebugOutput(true);
  _wm.setConnectRetries(8);
  _wm.setConfigPortalBlocking(false);
  _wm.setConfigPortalTimeout(portalTimeoutSec);

  delay(1500);

  bool restored = connectWithSdkSavedCredentials();
  if (!restored) {
    Serial.println("[WiFi] starting config portal / WiFiManager fallback");
    _wm.autoConnect(_apName, _apPass);
  }
  _lastReconnectAttemptMs = millis();
  _started = true;
}

void WifiManagerWrapper::update() {
  if (!_started) return;

  _wm.process();

  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED || st == WL_IDLE_STATUS) {
    return;
  }

  if (millis() - _lastReconnectAttemptMs <= 15000) {
    return;
  }

  _lastReconnectAttemptMs = millis();
  Serial.printf("[WiFi] reconnect attempt, status=%d\n", static_cast<int>(st));

  WiFi.disconnect(false, false);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  delay(300);

  if (WiFi.SSID().length() > 0) {
    Serial.println("[WiFi] reconnect attempt using SDK-saved credentials");
    WiFi.begin();
  } else {
    Serial.println("[WiFi] no SDK-saved credentials visible, requesting reconnect");
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
