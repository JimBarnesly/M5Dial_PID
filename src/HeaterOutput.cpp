#include "HeaterOutput.h"
#include "core/CoreConfig.h"
#include <Arduino.h>
#include <utility>

void HeaterOutput::begin() {
  writePin(false);
  _windowStartedMs = millis();
}

void HeaterOutput::setDriveHandler(std::function<void(bool on)> handler) { _driveHandler = std::move(handler); }

void HeaterOutput::setActiveHigh(bool activeHigh) { _activeHigh = activeHigh; }

void HeaterOutput::setEnabled(bool enabled) {
  _enabled = enabled;
  if (!_enabled) {
    writePin(false);
    _outputOn = false;
  }
}

void HeaterOutput::setOutputPercent(float percent) {
  _percent = constrain(percent, 0.0f, _maxPercent);
}

void HeaterOutput::setMaxOutputPercent(float percent) {
  _maxPercent = constrain(percent, 0.0f, 100.0f);
  _percent = constrain(_percent, 0.0f, _maxPercent);
}

void HeaterOutput::update() {
  if (!_enabled) {
    writePin(false);
    _outputOn = false;
    return;
  }

  const uint32_t now = millis();
  if (now - _windowStartedMs >= CoreConfig::PID_WINDOW_MS) {
    _windowStartedMs += CoreConfig::PID_WINDOW_MS;
  }

  const uint32_t onTimeMs = static_cast<uint32_t>((_percent / 100.0f) * CoreConfig::PID_WINDOW_MS);
  const bool shouldOn = (now - _windowStartedMs) < onTimeMs;

  if (shouldOn != _outputOn) {
    _outputOn = shouldOn;
    writePin(_outputOn);
  }
}

float HeaterOutput::getOutputPercent() const { return _percent; }
float HeaterOutput::getMaxOutputPercent() const { return _maxPercent; }
bool HeaterOutput::isOn() const { return _outputOn; }

void HeaterOutput::writePin(bool on) {
  if (_driveHandler) _driveHandler(_activeHigh ? on : !on);
}
