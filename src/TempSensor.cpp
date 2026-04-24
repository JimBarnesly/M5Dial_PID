#include "TempSensor.h"
#include "core/CoreConfig.h"

#include <algorithm>

namespace {
constexpr uint8_t kDs18b20FamilyCode = 0x28;

struct AddressSlot {
  DeviceAddress address {};
};

bool addressLess(const AddressSlot& lhs, const AddressSlot& rhs) {
  for (uint8_t i = 0; i < 8; ++i) {
    if (lhs.address[i] < rhs.address[i]) return true;
    if (lhs.address[i] > rhs.address[i]) return false;
  }
  return false;
}

void formatAddress(const DeviceAddress& address, char* out, size_t outSize) {
  if (outSize == 0) return;
  size_t used = 0;
  out[0] = '\0';
  for (uint8_t i = 0; i < 8 && used + 3 < outSize; ++i) {
    const int written = snprintf(out + used, outSize - used, "%02X", address[i]);
    if (written <= 0) break;
    used += static_cast<size_t>(written);
    if (i < 7 && used + 2 < outSize) {
      out[used++] = ':';
      out[used] = '\0';
    }
  }
}
}

TempSensor::TempSensor(uint8_t pin) : _oneWire(pin), _sensors(&_oneWire) {}

void TempSensor::begin() {
  _sensors.begin();
  _sensors.setWaitForConversion(false);
  scanBus();
  if (_probeA.present || _probeB.present) {
    _sensors.requestTemperatures();
    _lastRequestMs = millis();
  }
}

void TempSensor::update() {
  _newValue = false;

  if (!_probeA.present && !_probeB.present) {
    return;
  }

  if (millis() - _lastRequestMs < CoreConfig::TEMP_SAMPLE_MS) {
    return;
  }

  const bool probeAUpdated = sampleProbe(_probeA, 0, true);
  const bool probeBUpdated = _probeB.present ? sampleProbe(_probeB, 1, false) : false;
  if (probeAUpdated || probeBUpdated) {
    _newValue = probeAUpdated;
  }

  _sensors.requestTemperatures();
  _lastRequestMs = millis();
}

void TempSensor::scanBus() {
  _probeA = ProbeState();
  _probeB = ProbeState();
  _probeCount = 0;

  const uint8_t detectedCount = static_cast<uint8_t>(_sensors.getDeviceCount());
  AddressSlot addresses[CoreConfig::MAX_STAGES] {};
  uint8_t validCount = 0;

  for (uint8_t i = 0; i < detectedCount && i < CoreConfig::MAX_STAGES; ++i) {
    DeviceAddress address {};
    if (!_sensors.getAddress(address, i)) continue;
    if (!_sensors.validAddress(address)) continue;
    if (address[0] != kDs18b20FamilyCode) continue;
    memcpy(addresses[validCount].address, address, sizeof(DeviceAddress));
    ++validCount;
  }

  std::sort(addresses, addresses + validCount, addressLess);

  _probeCount = validCount;
  if (validCount > 0) {
    memcpy(_probeA.address, addresses[0].address, sizeof(DeviceAddress));
    _probeA.present = true;
  }
  if (validCount > 1) {
    memcpy(_probeB.address, addresses[1].address, sizeof(DeviceAddress));
    _probeB.present = true;
  }

  char addressText[24] {};
  Serial.printf("[Temp] detected probes=%u\n", static_cast<unsigned>(_probeCount));
  if (_probeA.present) {
    formatAddress(_probeA.address, addressText, sizeof(addressText));
    Serial.printf("[Temp] Probe A address=%s role=main_process_control\n", addressText);
  }
  if (_probeB.present) {
    formatAddress(_probeB.address, addressText, sizeof(addressText));
    Serial.printf("[Temp] Probe B address=%s role=optional_feed_forward_hlt\n", addressText);
  }
  if (_probeCount > 2) {
    Serial.printf("[Temp] extra probes found=%u ignoring probes beyond A/B\n",
                  static_cast<unsigned>(_probeCount - 2));
  }
  Serial.printf("[Temp] sensor mode=%s\n", _probeB.present ? "dual" : "single");
}

bool TempSensor::sampleProbe(ProbeState& probe, uint8_t probeIndex, bool isPrimaryRole) {
  probe.lastRawC = _sensors.getTempC(probe.address);
  probe.plausible = true;
  probe.healthy = probe.lastRawC > CoreConfig::SENSOR_FAULT_LOW_C &&
                  probe.lastRawC < CoreConfig::SENSOR_FAULT_HIGH_C &&
                  probe.lastRawC != DEVICE_DISCONNECTED_C;

  if (!probe.healthy) {
    if (isPrimaryRole && !_probeAFaultLogged) {
      Serial.printf("[Temp] Probe A fault/dropout detected index=%u raw=%.2f\n",
                    static_cast<unsigned>(probeIndex),
                    probe.lastRawC);
      _probeAFaultLogged = true;
    }
    if (!isPrimaryRole && !_probeBDropoutLogged) {
      Serial.printf("[Temp] Probe B dropout detected index=%u raw=%.2f fallback=probe_a_only\n",
                    static_cast<unsigned>(probeIndex),
                    probe.lastRawC);
      _probeBDropoutLogged = true;
    }
    return false;
  }

  const float calibratedC = probe.lastRawC + _calibrationOffsetC;
  const uint32_t now = millis();

  if (_maxRateCPerSec > 0.0f && !isnan(probe.lastC) && probe.lastAcceptedMs > 0) {
    const float dt = (now - probe.lastAcceptedMs) / 1000.0f;
    if (dt > 0.0f) {
      const float rate = fabsf(calibratedC - probe.lastC) / dt;
      if (rate > _maxRateCPerSec) {
        probe.plausible = false;
        probe.healthy = false;
      }
    }
  }

  if (!probe.healthy) {
    if (isPrimaryRole && !_probeAFaultLogged) {
      Serial.printf("[Temp] Probe A plausibility fault index=%u raw=%.2f\n",
                    static_cast<unsigned>(probeIndex),
                    probe.lastRawC);
      _probeAFaultLogged = true;
    }
    if (!isPrimaryRole && !_probeBDropoutLogged) {
      Serial.printf("[Temp] Probe B plausibility fault index=%u raw=%.2f fallback=probe_a_only\n",
                    static_cast<unsigned>(probeIndex),
                    probe.lastRawC);
      _probeBDropoutLogged = true;
    }
    return false;
  }

  if (!isnan(probe.lastC) && _smoothingAlpha > 0.0f && _smoothingAlpha < 1.0f) {
    probe.lastC = (_smoothingAlpha * calibratedC) + ((1.0f - _smoothingAlpha) * probe.lastC);
  } else {
    probe.lastC = calibratedC;
  }
  probe.lastAcceptedMs = now;

  if (isPrimaryRole && _probeAFaultLogged) {
    Serial.printf("[Temp] Probe A healthy again temp=%.2f\n", probe.lastC);
    _probeAFaultLogged = false;
  }
  if (!isPrimaryRole && _probeBDropoutLogged) {
    Serial.printf("[Temp] Probe B healthy again temp=%.2f feed_forward_available=1\n", probe.lastC);
    _probeBDropoutLogged = false;
  }

  return true;
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
bool TempSensor::isHealthy() const { return _probeA.present && _probeA.healthy; }
bool TempSensor::isPlausible() const { return _probeA.plausible; }
float TempSensor::getRawCelsius() const { return _probeA.lastRawC; }
float TempSensor::getCelsius() const { return _probeA.lastC; }
uint8_t TempSensor::getProbeCount() const { return _probeCount; }
bool TempSensor::isDualProbeMode() const { return _probeA.present && _probeB.present; }
bool TempSensor::isProbeAHealthy() const { return _probeA.present && _probeA.healthy; }
bool TempSensor::isProbeBHealthy() const { return _probeB.present && _probeB.healthy; }
bool TempSensor::isProbeAPlausible() const { return _probeA.plausible; }
bool TempSensor::isProbeBPlausible() const { return _probeB.present ? _probeB.plausible : false; }
float TempSensor::getProbeARawCelsius() const { return _probeA.lastRawC; }
float TempSensor::getProbeACelsius() const { return _probeA.lastC; }
float TempSensor::getProbeBRawCelsius() const { return _probeB.lastRawC; }
float TempSensor::getProbeBCelsius() const { return _probeB.lastC; }
