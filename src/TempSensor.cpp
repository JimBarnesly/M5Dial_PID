#include "TempSensor.h"
#include "Config.h"

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

  if (millis() - _lastRequestMs < Config::TEMP_SAMPLE_MS) {
    return;
  }

  float tempC = _sensors.getTempC(_address);
  _healthy = tempC > Config::SENSOR_FAULT_LOW_C &&
             tempC < Config::SENSOR_FAULT_HIGH_C &&
             tempC != DEVICE_DISCONNECTED_C;

  if (_healthy) {
    _lastC = tempC;
    _newValue = true;
  }

  _sensors.requestTemperatures();
  _lastRequestMs = millis();
}

bool TempSensor::hasNewValue() const { return _newValue; }
bool TempSensor::isHealthy() const { return _healthy; }
float TempSensor::getCelsius() const { return _lastC; }
