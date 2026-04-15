#pragma once

#include <DallasTemperature.h>
#include <OneWire.h>
#include "core/CoreConfig.h"

class TempSensor {
public:
  explicit TempSensor(uint8_t pin);
  void begin();
  void update();
  void setCalibrationOffset(float offsetC);
  void setSmoothingFactor(float alpha);
  void setPlausibilityLimit(float maxRateCPerSec);
  bool hasNewValue() const;
  bool isHealthy() const;
  bool isPlausible() const;
  float getRawCelsius() const;
  float getCelsius() const;

private:
  OneWire _oneWire;
  DallasTemperature _sensors;
  DeviceAddress _address {};
  float _lastRawC {NAN};
  float _lastC {NAN};
  float _calibrationOffsetC {0.0f};
  float _smoothingAlpha {CoreConfig::DEFAULT_TEMP_SMOOTHING_ALPHA};
  float _maxRateCPerSec {CoreConfig::DEFAULT_TEMP_MAX_RATE_C_PER_SEC};
  bool _healthy {false};
  bool _plausible {true};
  bool _newValue {false};
  uint32_t _lastRequestMs {0};
  uint32_t _lastAcceptedMs {0};
};
