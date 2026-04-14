#pragma once
#include <Arduino.h>
#include <functional>


class HeaterOutput {
public:
  HeaterOutput() = default;
  void begin();
  void setDriveHandler(std::function<void(bool on)> handler);
  void setActiveHigh(bool activeHigh);
  void setEnabled(bool enabled);
  void setOutputPercent(float percent);
  void setMaxOutputPercent(float percent);
  void update();
  float getOutputPercent() const;
  float getMaxOutputPercent() const;
  bool isOn() const;

private:
  void writePin(bool on);

  std::function<void(bool on)> _driveHandler;
  bool _activeHigh {true};
  bool _enabled {false};
  bool _outputOn {false};
  float _percent {0.0f};
  float _maxPercent {100.0f};
  uint32_t _windowStartedMs {0};

};
