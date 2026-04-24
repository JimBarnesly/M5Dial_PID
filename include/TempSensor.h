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
  uint8_t getProbeCount() const;
  bool isDualProbeMode() const;
  bool isProbeAHealthy() const;
  bool isProbeBHealthy() const;
  bool isProbeAPlausible() const;
  bool isProbeBPlausible() const;
  float getProbeARawCelsius() const;
  float getProbeACelsius() const;
  float getProbeBRawCelsius() const;
  float getProbeBCelsius() const;

private:
  // Probe A = main process/control probe.
  // Probe B = optional feed-forward / HLT probe.
  struct ProbeState {
    DeviceAddress address {};
    float lastRawC {NAN};
    float lastC {NAN};
    uint32_t lastAcceptedMs {0};
    bool present {false};
    bool healthy {false};
    bool plausible {true};
  };

  void scanBus();
  bool sampleProbe(ProbeState& probe, uint8_t probeIndex, bool isPrimaryRole);

  OneWire _oneWire;
  DallasTemperature _sensors;
  ProbeState _probeA {};
  ProbeState _probeB {};
  float _calibrationOffsetC {0.0f};
  float _smoothingAlpha {CoreConfig::DEFAULT_TEMP_SMOOTHING_ALPHA};
  float _maxRateCPerSec {CoreConfig::DEFAULT_TEMP_MAX_RATE_C_PER_SEC};
  bool _newValue {false};
  uint8_t _probeCount {0};
  bool _probeBDropoutLogged {false};
  bool _probeAFaultLogged {false};
  uint32_t _lastRequestMs {0};
};
