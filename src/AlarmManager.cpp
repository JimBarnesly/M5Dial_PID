#include "AlarmManager.h"
#include "core/CoreConfig.h"
#include <Arduino.h>
#include <cstring>
#include <utility>

void AlarmManager::begin() {
  setSignal(false);
}

void AlarmManager::setSignalHandler(std::function<void(bool on)> handler) { _signalHandler = std::move(handler); }
void AlarmManager::setCompletionHandler(std::function<void()> handler) { _completionHandler = std::move(handler); }

void AlarmManager::setSignal(bool on) {
  if (_signalHandler) _signalHandler(on);
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

void AlarmManager::clearAlarm(AlarmControlSource source) {
  if (source == AlarmControlSource::LocalUi && !_allowLocalUiAlarmControl) return;
  _alarm = AlarmCode::None;
  strncpy(_text, "OK", sizeof(_text) - 1);
  _text[sizeof(_text) - 1] = '\0';
  _acknowledged = false;
  _lastToggleMs = 0;
  _beepState = false;
  setSignal(false);
}

bool AlarmManager::acknowledge(AlarmControlSource source) {
  if (source == AlarmControlSource::LocalUi && !_allowLocalUiAlarmControl) return false;
  _acknowledged = true;
  _lastToggleMs = 0;
  _beepState = false;
  setSignal(false);
  return true;
}

void AlarmManager::setLocalUiAlarmControlEnabled(bool enabled) { _allowLocalUiAlarmControl = enabled; }

bool AlarmManager::isAcknowledged() const { return _acknowledged; }
AlarmCode AlarmManager::getAlarm() const { return _alarm; }
const char* AlarmManager::getText() const { return _text; }

void AlarmManager::notifyStageComplete() {
  if (_completionHandler) _completionHandler();
}

void AlarmManager::update() {
  const uint32_t now = millis();
  const bool alarmActive = (_alarm != AlarmCode::None) && !_acknowledged;

  if (alarmActive) {
    if (_lastToggleMs == 0) {
      _lastToggleMs = now;
      _beepState = true;
      setSignal(true);
    } else if (now - _lastToggleMs >= CoreConfig::ALARM_BEEP_MS) {
      _lastToggleMs = now;
      _beepState = !_beepState;
      setSignal(_beepState);
    }
  } else {
    setSignal(false);
    _beepState = false;
    _lastToggleMs = 0;
  }
}
