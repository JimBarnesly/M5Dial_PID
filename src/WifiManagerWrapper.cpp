#include "WifiManagerWrapper.h"
#include "core/CoreConfig.h"
#include <WiFi.h>

namespace {
constexpr char kForcedSsid[] = "project6";
constexpr char kForcedPass[] = "sIlver@99";
}

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

  // ESP32 can report an empty PSK even when connected to a secured AP.
  // Never overwrite a known password with an empty value.
  if (currentPass.isEmpty() && currentSsid == _savedSsid && !_savedPass.isEmpty()) {
    _lastPersistedSsid = currentSsid;
    Serial.printf("[WiFi] skip credential write for SSID=%s (empty PSK would overwrite saved password)\n",
                  currentSsid.c_str());
    return;
  }

  if (currentPass.isEmpty() && currentSsid != _savedSsid) {
    Serial.printf("[WiFi] skip credential write for SSID=%s (empty PSK for new network)\n",
                  currentSsid.c_str());
    return;
  }

  _wifiPrefs.putString("ssid", currentSsid);
  if (!currentPass.isEmpty()) _wifiPrefs.putString("pass", currentPass);
  _savedSsid = currentSsid;
  if (!currentPass.isEmpty()) _savedPass = currentPass;
  _lastPersistedSsid = currentSsid;
  _haveSavedCredentials = true;
  Serial.printf("[WiFi] persisted credentials for SSID=%s (pass_saved=%d)\n",
                currentSsid.c_str(),
                !currentPass.isEmpty());
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
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.persistent(true);
  _wm.setDebugOutput(true);
  _wm.setConnectRetries(8);
  _wm.setConfigPortalBlocking(false);
  _wm.setConfigPortalTimeout(portalTimeoutSec);

  Serial.printf("[WiFi] attempting forced credentials SSID=%s\n", kForcedSsid);
  WiFi.begin(kForcedSsid, kForcedPass);
  const uint32_t forcedStart = millis();
  while (millis() - forcedStart < 15000) {
    if (WiFi.status() == WL_CONNECTED) break;
    delay(50);
  }

  if (WiFi.status() == WL_CONNECTED) {
    _savedSsid = kForcedSsid;
    _savedPass = kForcedPass;
    _haveSavedCredentials = true;
    saveCurrentCredentials();
  } else {
    const bool restored = connectWithSavedCredentials();
    if (!restored) _wm.autoConnect(_apName, _apPass);
  }
  _started = true;
}

void WifiManagerWrapper::update() {
  if (_started) {
    _wm.process();
    if (WiFi.status() == WL_CONNECTED) saveCurrentCredentials();
    if (WiFi.status() != WL_CONNECTED && millis() - _lastReconnectAttemptMs > 10000) {
      _lastReconnectAttemptMs = millis();
      Serial.println("[WiFi] reconnect attempt using forced credentials");
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
  loadSavedCredentials();
  _wifiPrefs.remove("ssid");
  _wifiPrefs.remove("pass");
  _savedSsid = "";
  _savedPass = "";
  _lastPersistedSsid = "";
  _haveSavedCredentials = false;
}
