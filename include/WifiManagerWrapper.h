#pragma once
#include <WiFiManager.h>
#include <Preferences.h>

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
  void loadSavedCredentials();
  void saveCurrentCredentials();
  bool connectWithSavedCredentials(uint32_t timeoutMs = 15000);
  bool connectWithSdkSavedCredentials(uint32_t timeoutMs = 15000);
  WiFiManager _wm;
  Preferences _wifiPrefs;
  bool _started {false};
  bool _haveSavedCredentials {false};
  bool _credentialsLoaded {false};
  String _savedSsid;
  String _savedPass;
  String _lastPersistedSsid;
  uint32_t _lastReconnectAttemptMs {0};
  char _apName[32] {};
  char _apPass[20] {};
};
