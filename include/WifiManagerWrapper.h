#pragma once
#include <WiFiManager.h>

class WifiManagerWrapper {
public:
  void begin();
  void update();
  bool isConnected() const;

private:
  WiFiManager _wm;
  bool _started {false};
};
