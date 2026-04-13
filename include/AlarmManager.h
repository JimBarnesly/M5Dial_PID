#pragma once
#include <functional>
#include "AppState.h"

enum class AlarmControlSource : uint8_t {
  System = 0,
  LocalUi,
  RemoteMqtt
};

class AlarmManager {
public:
  AlarmManager() = default;
  void begin();
  void setSignalHandler(std::function<void(bool on)> handler);
  void setAlarm(AlarmCode code, const char* text, bool beep = true);
  void clearAlarm(AlarmControlSource source = AlarmControlSource::System);
  bool acknowledge(AlarmControlSource source = AlarmControlSource::System);
  void setLocalUiAlarmControlEnabled(bool enabled);
  bool isAcknowledged() const;
  AlarmCode getAlarm() const;
  const char* getText() const;
  void notifyStageComplete();
  void update();

private:
  void setSignal(bool on);

  std::function<void(bool on)> _signalHandler;
  AlarmCode _alarm {AlarmCode::None};
  char _text[64] {"OK"};
  uint32_t _notifyBeepUntilMs {0};
  uint32_t _lastToggleMs {0};
  bool _beepState {false};
  bool _acknowledged {false};
  bool _allowLocalUiAlarmControl {true};
};
