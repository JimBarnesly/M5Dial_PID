#include "platform/m5dial/M5DialBuzzer.h"
#include "core/CoreConfig.h"

M5DialBuzzer::M5DialBuzzer(uint8_t pin) : _pin(pin) {}

void M5DialBuzzer::begin() {
  pinMode(_pin, OUTPUT);
  stopTone();
}

void M5DialBuzzer::set(bool on) {
  _alarmOn = on;
  if (on) {
    _patternActive = false;
    _patternToneOn = false;
    _remainingPatternBeeps = 0;
  }
  applyOutput(millis());
}

void M5DialBuzzer::playCompletionPattern() {
  if (_alarmOn) return;

  _patternActive = true;
  _patternToneOn = true;
  _remainingPatternBeeps = CoreConfig::BUZZER_COMPLETION_BEEP_COUNT;
  _phaseDeadlineMs = millis() + CoreConfig::BUZZER_COMPLETION_BEEP_MS;
  applyOutput(millis());
}

void M5DialBuzzer::update() {
  applyOutput(millis());
}

void M5DialBuzzer::applyOutput(uint32_t now) {
  if (_alarmOn) {
    startTone(CoreConfig::BUZZER_ALARM_FREQ_HZ);
    return;
  }

  if (!_patternActive) {
    stopTone();
    return;
  }

  if (now >= _phaseDeadlineMs) {
    if (_patternToneOn) {
      if (_remainingPatternBeeps > 0) --_remainingPatternBeeps;
      if (_remainingPatternBeeps == 0) {
        _patternActive = false;
        _patternToneOn = false;
        stopTone();
        return;
      }
      _patternToneOn = false;
      _phaseDeadlineMs = now + CoreConfig::BUZZER_COMPLETION_GAP_MS;
    } else {
      _patternToneOn = true;
      _phaseDeadlineMs = now + CoreConfig::BUZZER_COMPLETION_BEEP_MS;
    }
  }

  if (_patternToneOn) startTone(CoreConfig::BUZZER_COMPLETION_FREQ_HZ);
  else stopTone();
}

void M5DialBuzzer::startTone(uint16_t frequencyHz) {
  if (_currentFrequencyHz == frequencyHz) return;
  tone(_pin, frequencyHz);
  _currentFrequencyHz = frequencyHz;
}

void M5DialBuzzer::stopTone() {
  if (_currentFrequencyHz == 0) return;
  noTone(_pin);
  _currentFrequencyHz = 0;
}
