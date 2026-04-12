#pragma once
#include <WiFiManager.h>

class WifiManagerWrapper {
public:
  void begin(uint16_t portalTimeoutSec = 180);
  void update();
  bool isConnected() const;
  const char* getPortalApName() const;
  const char* getPortalApPassword() const;

private:
  void buildPortalCredentials();
  WiFiManager _wm;
  bool _started {false};
  char _apName[32] {};
  char _apPass[20] {};
};
