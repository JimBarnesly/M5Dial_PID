#pragma once
#include <M5Dial.h>
#include "AppState.h"

class DisplayManager {
public:
  void begin();
  void draw(const PersistentConfig& cfg, const RuntimeState& rt, const BrewStage* stage, uint32_t remainingSec);
  void invalidateAll();

private:
  void drawStaticUi();
  void drawRing(float progress);
  void drawCenterTemp(const RuntimeState& rt, uint32_t now);
  void drawSetpoint(const RuntimeState& rt, bool force = false);
  void drawTimeAndPower(const RuntimeState& rt, uint32_t remainingSec, bool force = false);
  void drawTopState(const RuntimeState& rt, bool force = false);
  void drawStageName(const BrewStage* stage, bool force = false);
  void drawAlarm(const RuntimeState& rt, bool force = false);
  void drawEditBadge(const RuntimeState& rt, bool force = false);
  void drawNetStatus(const RuntimeState& rt, bool force = false);
  String formatTime(uint32_t sec);

  const uint16_t BG = 0x0000;
  const uint16_t FG = 0xFFDE;
  const uint16_t GOLD = 0xF5C0;
  const uint16_t BLUE = 0x25BF;
  const uint16_t RED = 0xE2E8;
  const uint16_t GREY = 0x4208;
  const uint16_t MID = 0x18E3;
  const uint16_t BTN = 0x3186;
  const uint16_t GREEN = 0x07E0;

  bool _staticDrawn {false};
  bool _forceFull {true};
  bool _ringDirty {true};
  uint32_t _lastUiServiceMs {0};
  uint32_t _lastTempDrawMs {0};
  uint32_t _lastStatusDrawMs {0};
  uint32_t _lastRingDrawMs {0};
  uint32_t _lastForceRefreshMs {0};

  float _lastTempC {NAN};
  float _lastSetpointC {NAN};
  float _lastHeaterPct {-999.0f};
  uint32_t _lastRemainingSec {UINT32_MAX};
  bool _lastWifiConnected {false};
  bool _lastMqttConnected {false};
  bool _lastEditMode {false};
  AlarmCode _lastAlarm {AlarmCode::None};
  char _lastAlarmText[64] {0};
  char _lastTopState[40] {0};
  char _lastStageName[24] {0};
};
