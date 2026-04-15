#include "TempSensor.h"
#include "core/CoreConfig.h"

TempSensor::TempSensor(uint8_t pin) : _oneWire(pin), _sensors(&_oneWire) {}

void TempSensor::begin() {
  _sensors.begin();
  _sensors.setWaitForConversion(false);
  if (_sensors.getDeviceCount() > 0) {
    _sensors.getAddress(_address, 0);
    _sensors.requestTemperatures();
    _lastRequestMs = millis();
  }
}

void TempSensor::update() {
  _newValue = false;

  if (millis() - _lastRequestMs < CoreConfig::TEMP_SAMPLE_MS) {
    return;
  }

  float tempC = _sensors.getTempC(_address);
  _lastRawC = tempC;
  _plausible = true;
  _healthy = tempC > CoreConfig::SENSOR_FAULT_LOW_C &&
             tempC < CoreConfig::SENSOR_FAULT_HIGH_C &&
             tempC != DEVICE_DISCONNECTED_C;

  if (_healthy) {
    const float calibratedC = tempC + _calibrationOffsetC;
    const uint32_t now = millis();

    if (_maxRateCPerSec > 0.0f && !isnan(_lastC) && _lastAcceptedMs > 0) {
      const float dt = (now - _lastAcceptedMs) / 1000.0f;
      if (dt > 0.0f) {
        const float rate = fabsf(calibratedC - _lastC) / dt;
        if (rate > _maxRateCPerSec) {
          _plausible = false;
          _healthy = false;
        }
      }
    }

    if (_healthy) {
      if (!isnan(_lastC) && _smoothingAlpha > 0.0f && _smoothingAlpha < 1.0f) {
        _lastC = (_smoothingAlpha * calibratedC) + ((1.0f - _smoothingAlpha) * _lastC);
      } else {
        _lastC = calibratedC;
      }
      _lastAcceptedMs = now;
      _newValue = true;
    }
  }

  _sensors.requestTemperatures();
  _lastRequestMs = millis();
}

void TempSensor::setCalibrationOffset(float offsetC) {
  _calibrationOffsetC = offsetC;
}

void TempSensor::setSmoothingFactor(float alpha) {
  _smoothingAlpha = constrain(alpha, 0.0f, 1.0f);
}

void TempSensor::setPlausibilityLimit(float maxRateCPerSec) {
  _maxRateCPerSec = max(0.0f, maxRateCPerSec);
}

bool TempSensor::hasNewValue() const { return _newValue; }
bool TempSensor::isHealthy() const { return _healthy; }
bool TempSensor::isPlausible() const { return _plausible; }
float TempSensor::getRawCelsius() const { return _lastRawC; }
float TempSensor::getCelsius() const { return _lastC; }
