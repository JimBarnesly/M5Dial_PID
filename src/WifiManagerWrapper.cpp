#include "WifiManagerWrapper.h"
#include "Config.h"
#include <WiFi.h>

void WifiManagerWrapper::begin() {
  _wm.setConfigPortalBlocking(false);
  _wm.setConfigPortalTimeout(180);
  _wm.autoConnect(Config::WIFI_AP_NAME, Config::WIFI_AP_PASS);
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
