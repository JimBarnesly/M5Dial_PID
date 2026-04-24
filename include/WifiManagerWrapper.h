#pragma once
#include <WiFiManager.h>

class WifiManagerWrapper {
public:
  void beginStandalone(uint16_t portalTimeoutSec = 180);
  void beginIntegrated(const char* ssid, const char* password);
  void update();
  bool isConnected() const;
  const char* getPortalApName() const;
  const char* getPortalApPassword() const;
  void resetSettings();

private:
  void buildPortalCredentials();
  WiFiManager _wm;
  bool _started {false};
  bool _portalForced {false};
  bool _disableForcedCredentials {false};
  uint8_t _authExpireCount {0};
  uint32_t _lastAuthExpireMs {0};
  uint32_t _lastReconnectAttemptMs {0};
  bool _integratedMode {false};
  char _apName[32] {};
  char _apPass[20] {};
  char _managedSsid[33] {};
  char _managedPass[65] {};
};
