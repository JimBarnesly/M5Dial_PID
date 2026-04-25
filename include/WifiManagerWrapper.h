#pragma once
#include <WiFiManager.h>

class WifiManagerWrapper {
public:
  enum class State : uint8_t {
    Idle = 0,
    Connecting,
    PortalActive,
    Connected
  };

  void beginStandalone(uint16_t portalTimeoutSec = 180);
  void beginIntegrated(const char* ssid, const char* password);
  void update();
  bool isConnected() const;
  const char* getPortalApName() const;
  const char* getPortalApPassword() const;
  void resetSettings();
  State state() const { return _state; }

private:
  void buildPortalCredentials();
  void transitionTo(State next);
  bool hasSavedCredentials() const;
  void startBackgroundPortal();
  void ensureEventLogging();
  WiFiManager _wm;
  bool _started {false};
  bool _eventLoggingInstalled {false};
  bool _portalForced {false};
  uint8_t _authExpireCount {0};
  uint32_t _lastAuthExpireMs {0};
  uint32_t _lastReconnectAttemptMs {0};
  uint16_t _portalTimeoutSec {180};
  bool _integratedMode {false};
  State _state {State::Idle};
  char _apName[32] {};
  char _apPass[20] {};
  char _managedSsid[33] {};
  char _managedPass[65] {};
};
