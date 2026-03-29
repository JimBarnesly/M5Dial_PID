#include "AlarmManager.h"
#include "Config.h"
#include <Arduino.h>
#include <cstring>

AlarmManager::AlarmManager(uint8_t buzzerPin) : _buzzerPin(buzzerPin) {}

void AlarmManager::begin() {
  pinMode(_buzzerPin, OUTPUT);
  digitalWrite(_buzzerPin, LOW);
}

void AlarmManager::setAlarm(AlarmCode code, const char* text, bool beep) {
  _alarm = code;
  strncpy(_text, text, sizeof(_text) - 1);
  _text[sizeof(_text) - 1] = '\0';
  if (beep) {
    _beepUntilMs = millis() + Config::ALARM_BEEP_MS;
    _lastToggleMs = 0;
  }
}

void AlarmManager::clearAlarm() {
  _alarm = AlarmCode::None;
  strncpy(_text, "OK", sizeof(_text) - 1);
  _text[sizeof(_text) - 1] = '\0';
}

AlarmCode AlarmManager::getAlarm() const { return _alarm; }
const char* AlarmManager::getText() const { return _text; }

void AlarmManager::notifyStageComplete() {
  _beepUntilMs = millis() + Config::HOLD_COMPLETE_BEEP_MS;
  _lastToggleMs = 0;
}

void AlarmManager::update() {
  const uint32_t now = millis();
  if (now < _beepUntilMs) {
    if (_lastToggleMs == 0 || now - _lastToggleMs >= Config::BUZZER_TOGGLE_MS) {
      _lastToggleMs = now;
      _beepState = !_beepState;
      digitalWrite(_buzzerPin, _beepState ? HIGH : LOW);
    }
  } else {
    digitalWrite(_buzzerPin, LOW);
    _beepState = false;
    _lastToggleMs = 0;
  }
}
