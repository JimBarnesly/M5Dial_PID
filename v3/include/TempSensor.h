#pragma once

#include <DallasTemperature.h>
#include <OneWire.h>

class TempSensor {
public:
  explicit TempSensor(uint8_t pin);
  void begin();
  void update();
  bool hasNewValue() const;
  bool isHealthy() const;
  float getCelsius() const;

private:
  OneWire _oneWire;
  DallasTemperature _sensors;
  DeviceAddress _address {};
  float _lastC {NAN};
  bool _healthy {false};
  bool _newValue {false};
  uint32_t _lastRequestMs {0};
};
