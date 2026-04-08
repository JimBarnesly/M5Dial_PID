#pragma once
#include "AppState.h"

class AlarmManager {
public:
  explicit AlarmManager(uint8_t buzzerPin);
  void begin();
  void setAlarm(AlarmCode code, const char* text, bool beep = true);
  void clearAlarm();
  void acknowledge();
  bool isAcknowledged() const;
  AlarmCode getAlarm() const;
  const char* getText() const;
  void notifyStageComplete();
  void update();

private:
  uint8_t _buzzerPin;
  AlarmCode _alarm {AlarmCode::None};
  char _text[64] {"OK"};
  uint32_t _notifyBeepUntilMs {0};
  uint32_t _lastToggleMs {0};
  bool _beepState {false};
  bool _acknowledged {false};
};
