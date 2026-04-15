#include "WifiManagerWrapper.h"
#include "core/CoreConfig.h"
#include <WiFi.h>
#include <cstdlib>

namespace {
WifiManagerWrapper* gActiveWifiWrapper = nullptr;
}

void WifiManagerWrapper::onSaveConfigCallback() {
  if (gActiveWifiWrapper) {
    gActiveWifiWrapper->_pendingConfigUpdate = true;
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
  if (defaultMqttHost && defaultMqttHost[0] != '\0') {
    strlcpy(_mqttHost, defaultMqttHost, sizeof(_mqttHost));
  }
  snprintf(_mqttPort, sizeof(_mqttPort), "%u", defaultMqttPort);

  if (!_mqttHostParam) {
    _mqttHostParam = new WiFiManagerParameter("mqtt_host", "MQTT host", _mqttHost, sizeof(_mqttHost));
  }
  if (!_mqttPortParam) {
    _mqttPortParam = new WiFiManagerParameter("mqtt_port", "MQTT port", _mqttPort, sizeof(_mqttPort));
  }

  buildPortalCredentials();
  gActiveWifiWrapper = this;
  _wm.setDebugOutput(true);
  _wm.addParameter(_mqttHostParam);
  _wm.addParameter(_mqttPortParam);
  _wm.setSaveConfigCallback(WifiManagerWrapper::onSaveConfigCallback);
  _wm.setConfigPortalBlocking(false);
  _wm.setConfigPortalTimeout(portalTimeoutSec);
  _wm.autoConnect(_apName, _apPass);
  strlcpy(_mqttHost, _mqttHostParam->getValue(), sizeof(_mqttHost));
  strlcpy(_mqttPort, _mqttPortParam->getValue(), sizeof(_mqttPort));
  applyPortalValues();
  _started = true;
}

void WifiManagerWrapper::update() {
  if (_started) {
    _wm.process();
    if (_pendingConfigUpdate) applyPortalValues();
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

bool WifiManagerWrapper::hasPendingConfigUpdate() const { return _pendingConfigUpdate; }

void WifiManagerWrapper::clearPendingConfigUpdate() { _pendingConfigUpdate = false; }

const char* WifiManagerWrapper::getConfiguredMqttHost() const { return _mqttHost; }

uint16_t WifiManagerWrapper::getConfiguredMqttPort() const {
  const int parsed = atoi(_mqttPort);
  if (parsed <= 0 || parsed > 65535) return CoreConfig::MQTT_PORT_PLAIN;
  return static_cast<uint16_t>(parsed);
}

void WifiManagerWrapper::applyPortalValues() {
  if (_mqttHost[0] == '\0') strlcpy(_mqttHost, "192.168.1.10", sizeof(_mqttHost));
  const int parsed = atoi(_mqttPort);
  if (parsed <= 0 || parsed > 65535) strlcpy(_mqttPort, "1883", sizeof(_mqttPort));
}

void WifiManagerWrapper::resetSettings() {
  _wm.resetSettings();
  _pendingConfigUpdate = false;
}
