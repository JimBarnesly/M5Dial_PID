#include "HeaterOutput.h"
#include "Config.h"
#include <Arduino.h>

HeaterOutput::HeaterOutput(uint8_t pin, bool activeHigh) : _pin(pin), _activeHigh(activeHigh) {}

void HeaterOutput::begin() {
  pinMode(_pin, OUTPUT);
  writePin(false);
  _windowStartedMs = millis();
}

void HeaterOutput::setEnabled(bool enabled) {
  _enabled = enabled;
  if (!_enabled) {
    writePin(false);
    _outputOn = false;
  }
}

void HeaterOutput::setOutputPercent(float percent) {
  _percent = constrain(percent, 0.0f, 100.0f);
}

void HeaterOutput::update() {
  if (!_enabled) {
    writePin(false);
    _outputOn = false;
    return;
  }

  const uint32_t now = millis();
  if (now - _windowStartedMs >= Config::PID_WINDOW_MS) {
    _windowStartedMs += Config::PID_WINDOW_MS;
  }

  const uint32_t onTimeMs = static_cast<uint32_t>((_percent / 100.0f) * Config::PID_WINDOW_MS);
  const bool shouldOn = (now - _windowStartedMs) < onTimeMs;

  if (shouldOn != _outputOn) {
    _outputOn = shouldOn;
    writePin(_outputOn);
  }
}

float HeaterOutput::getOutputPercent() const { return _percent; }
bool HeaterOutput::isOn() const { return _outputOn; }

void HeaterOutput::writePin(bool on) {
  digitalWrite(_pin, (_activeHigh ? on : !on) ? HIGH : LOW);
}
