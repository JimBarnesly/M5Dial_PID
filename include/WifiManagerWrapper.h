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
  bool hasPendingConfigUpdate() const;
  void clearPendingConfigUpdate();
  const char* getConfiguredMqttHost() const;
  uint16_t getConfiguredMqttPort() const;
  void resetSettings();

private:
  static void onSaveConfigCallback();
  void buildPortalCredentials();
  void applyPortalValues();
  WiFiManager _wm;
  bool _started {false};
  bool _pendingConfigUpdate {false};
  char _apName[32] {};
  char _apPass[20] {};
  char _mqttHost[64] {"192.168.1.10"};
  char _mqttPort[8] {"1883"};
};
