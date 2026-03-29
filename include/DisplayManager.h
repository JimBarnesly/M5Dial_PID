#pragma once
#include <M5Dial.h>
#include "AppState.h"

class DisplayManager {
public:
  void begin();
  void draw(const PersistentConfig& cfg, const RuntimeState& rt, const BrewStage* stage, uint32_t remainingSec);
  void invalidateAll();
  void requestImmediateUi();

private:
  void drawStaticUi();
  void drawRing(float progress, bool force = false);
  //void drawBrand(bool force = false);
  void drawStagePill(const RuntimeState& rt, const BrewStage* stage, bool force = false);
  void drawCenterTemp(const RuntimeState& rt, uint32_t now, bool force = false);
  void drawTargetRow(const RuntimeState& rt, bool force = false);
  void drawInfoRow(const RuntimeState& rt, uint32_t remainingSec, bool force = false);
  void drawHeatIcon(const RuntimeState& rt, bool force = false);
  void drawWifiIcon(const RuntimeState& rt, bool force = false);
  void drawAlarmOverlay(const RuntimeState& rt, bool force = false);
  void drawBottomControls(bool force = false);
  void drawSettingsIcon(int x, int y, uint16_t color);
  void drawStopButton();
  void drawWifiGlyph(int x, int y, uint16_t color);
  void drawFireGlyph(int x, int y, uint16_t color);
  String formatTime(uint32_t sec);

  static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  }

  const uint16_t BG = rgb(14, 14, 14);
  const uint16_t SURFACE_LOW = rgb(19, 19, 19);
  const uint16_t SURFACE = rgb(26, 26, 26);
  const uint16_t SURFACE_HIGH = rgb(32, 32, 31);
  const uint16_t SURFACE_HIGHEST = rgb(38, 38, 38);
  const uint16_t FG = rgb(255, 255, 255);
  const uint16_t FG_MUTED = rgb(173, 170, 170);
  const uint16_t OUTLINE = rgb(118, 117, 117);
  const uint16_t OUTLINE_SOFT = rgb(72, 72, 71);
  const uint16_t GOLD = rgb(255, 197, 99);
  const uint16_t GOLD_DIM = rgb(237, 166, 0);
  const uint16_t BLUE = rgb(58, 162, 255);
  const uint16_t BLUE_DARK = rgb(0, 97, 164);
  const uint16_t RED = rgb(255, 113, 97);
  const uint16_t RED_DARK = rgb(210, 60, 50);

  bool _staticDrawn {false};
  bool _forceFull {true};
  bool _ringDirty {true};
  uint32_t _lastUiServiceMs {0};
  uint32_t _lastTempDrawMs {0};
  uint32_t _lastRingDrawMs {0};

  float _lastTempC {NAN};
  float _lastSetpointC {NAN};
  float _lastHeaterPct {-999.0f};
  uint32_t _lastRemainingSec {UINT32_MAX};
  bool _lastWifiConnected {false};
  bool _lastMqttConnected {false};
  bool _lastHeatOn {false};
  bool _lastEditMode {false};
  AlarmCode _lastAlarm {static_cast<AlarmCode>(255)};
  char _lastAlarmText[64] {0};
  char _lastStageName[24] {0};
};
