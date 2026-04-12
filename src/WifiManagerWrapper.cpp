#include "WifiManagerWrapper.h"
#include "Config.h"
#include <WiFi.h>
#include <esp_system.h>

void WifiManagerWrapper::buildPortalCredentials() {
  uint64_t efuseMac = ESP.getEfuseMac();
  uint32_t suffix = static_cast<uint32_t>(efuseMac & 0xFFFFFFULL);
  snprintf(_apName, sizeof(_apName), "%s%06lX", Config::WIFI_AP_NAME_PREFIX, static_cast<unsigned long>(suffix));
  snprintf(_apPass, sizeof(_apPass), "BC%08lX%02X",
           static_cast<unsigned long>((efuseMac >> 8) & 0xFFFFFFFFULL),
           static_cast<unsigned>(suffix & 0xFF));
  _apPass[Config::WIFI_AP_PASS_LEN] = '\0';
}

void WifiManagerWrapper::begin() {
  buildPortalCredentials();
  _wm.setConfigPortalBlocking(false);
  _wm.setConfigPortalTimeout(180);
  _wm.autoConnect(_apName, _apPass);
  _started = true;
}

void WifiManagerWrapper::update() {
  if (_started) {
    _wm.process();
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
