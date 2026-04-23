#pragma once
#include <WiFiManager.h>

class WifiManagerWrapper {
public:
  void begin(uint16_t portalTimeoutSec = 180,
             const char* defaultMqttHost = "",
             uint16_t defaultMqttPort = 1883);
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
  char _apName[32] {};
  char _apPass[20] {};
};
