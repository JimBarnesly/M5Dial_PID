#pragma once
#include <WiFiManager.h>
#include <WiFi.h>

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
  void handleWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
  WiFiManager _wm;
  WiFiEventId_t _wifiEventHandler {};
  bool _started {false};
  bool _portalForcedDueToAuthExpire {false};
  uint8_t _authExpireCount {0};
  uint32_t _lastReconnectAttemptMs {0};
  char _apName[32] {};
  char _apPass[20] {};
};
