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
  const bool changed = (_alarm != code) || (strncmp(_text, text, sizeof(_text) - 1) != 0);
  _alarm = code;
  strncpy(_text, text, sizeof(_text) - 1);
  _text[sizeof(_text) - 1] = '\0';
  if (changed) {
    _acknowledged = false;
    _lastToggleMs = 0;
    _beepState = false;
  }
  if (!beep) {
    _acknowledged = true;
  }
}

void AlarmManager::clearAlarm() {
  _alarm = AlarmCode::None;
  strncpy(_text, "OK", sizeof(_text) - 1);
  _text[sizeof(_text) - 1] = '\0';
  _acknowledged = false;
  _notifyBeepUntilMs = 0;
  _lastToggleMs = 0;
  _beepState = false;
  digitalWrite(_buzzerPin, LOW);
}

void AlarmManager::acknowledge() {
  _acknowledged = true;
  _notifyBeepUntilMs = 0;
  _lastToggleMs = 0;
  _beepState = false;
  digitalWrite(_buzzerPin, LOW);
}

bool AlarmManager::isAcknowledged() const { return _acknowledged; }
AlarmCode AlarmManager::getAlarm() const { return _alarm; }
const char* AlarmManager::getText() const { return _text; }

void AlarmManager::notifyStageComplete() {
  _notifyBeepUntilMs = millis() + Config::HOLD_COMPLETE_BEEP_MS;
  _lastToggleMs = 0;
  _beepState = false;
}

void AlarmManager::update() {
  const uint32_t now = millis();
  const bool alarmActive = (_alarm != AlarmCode::None) && !_acknowledged;
  const bool notifyActive = now < _notifyBeepUntilMs;

  if (alarmActive || notifyActive) {
    if (_lastToggleMs == 0) {
      _lastToggleMs = now;
      _beepState = true;
      digitalWrite(_buzzerPin, HIGH);
    } else if (now - _lastToggleMs >= Config::ALARM_BEEP_MS) {
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
