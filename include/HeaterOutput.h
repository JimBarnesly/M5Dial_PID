#pragma once
#include <Arduino.h>


class HeaterOutput {
public:
  explicit HeaterOutput(uint8_t pin, bool activeHigh = true);
  void begin();
  void setEnabled(bool enabled);
  void setOutputPercent(float percent);
  void setMaxOutputPercent(float percent);
  void update();
  float getOutputPercent() const;
  float getMaxOutputPercent() const;
  bool isOn() const;

private:
  uint8_t _pin;
  bool _activeHigh;
  bool _enabled {false};
  bool _outputOn {false};
  float _percent {0.0f};
  float _maxPercent {100.0f};
  uint32_t _windowStartedMs {0};

  void writePin(bool on);
};
