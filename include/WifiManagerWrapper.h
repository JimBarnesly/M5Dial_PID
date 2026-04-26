#pragma once
#include <Arduino.h>

class WifiManagerWrapper {
public:
  enum class State : uint8_t {
    Idle = 0,
    Connecting,
    Connected,
    RetryBackoff,
    Unavailable
  };

  void beginStandalone();
  void beginIntegrated(const char* ssid, const char* password);
  void update();
  bool isConnected() const;
  void resetSettings();
  State state() const { return _state; }

private:
  void transitionTo(State next);
  void ensureEventLogging();
  void restartStaAndConnect(const char* ssid, const char* password);
  bool hasManagedCredentials() const;
  bool _started {false};
  bool _eventLoggingInstalled {false};
  uint32_t _lastReconnectAttemptMs {0};
  uint32_t _connectAllowedAtMs {0};
  bool _integratedMode {false};
  State _state {State::Idle};
  char _managedSsid[33] {};
  char _managedPass[65] {};
};
