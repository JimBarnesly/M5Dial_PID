#pragma once

#include <Arduino.h>
#include <M5Dial.h>
#include "Config.h"
#include "AppState.h"
#include "MenuSystem.h"

class DisplayManager {
public:
  void begin();
  void invalidateAll();
  void requestImmediateUi();
  bool wasSettingsTouched();
  bool wasAlarmPillTouched();
  void draw(const PersistentConfig& cfg, const RuntimeState& rt, const ProcessStage* stage, uint32_t remainingSec, const MenuRenderState* menuState = nullptr);

private:
  String formatTime(uint32_t sec);
  String formatMinutes(uint32_t minutes);

  bool menuStateChanged(const MenuRenderState& menuState) const;
  void cacheMenuState(const MenuRenderState& menuState);
  void drawStaticUi();
  void drawMenuScreen(const MenuRenderState& menuState);
  void drawMessageScreen(const char* title, const char* detail, const char* footer);
  void drawRing(float progress, bool timerStarted, RunState runState, uint32_t remainingSec, bool force = false);
  void drawStagePill(const RuntimeState& rt, const ProcessStage* stage, bool force = false);
  void drawCenterTemp(const RuntimeState& rt, uint32_t now, bool force = false);
  void drawTargetRow(const RuntimeState& rt, bool force = false);
  void drawInfoRow(const RuntimeState& rt, uint32_t remainingSec, bool force = false);
  void drawTestingBadge(const RuntimeState& rt, bool force = false);
  void drawHeatIcon(const RuntimeState& rt, bool force = false);
  void drawWifiIcon(const RuntimeState& rt, bool force = false);
  void drawWifiGlyph(int x, int y, uint16_t color);
  void drawNoIntegrationOverlay(int x, int y);
  void drawFireGlyph(int x, int y, uint16_t color);
  void drawSettingsIcon(int x, int y, uint16_t color);
  bool hitRectPressed(int x, int y, int w, int h);

  bool _lastStageTimerStarted = false;
  bool _staticDrawn = false;
  bool _forceFull = true;
  bool _settingsTouched = false;
  bool _alarmPillTouched = false;
  bool _ringDirty = true;

  uint32_t _lastUiServiceMs = 0;
  uint32_t _lastTempDrawMs = 0;
  uint32_t _lastRingDrawMs = 0;
  uint32_t _lastRingRemainingSec = UINT32_MAX;
  uint32_t _lastRemainingSec = UINT32_MAX;
  int _lastRingSweep = -1;
  uint8_t _lastRingMode = 0;

  float _lastTempC = NAN;
  float _lastSetpointC = NAN;
  bool _lastHeatOn = false;
  bool _lastWifiConnected = false;
  bool _lastMqttConnected = false;
  bool _lastTestingModeActive = false;
  OperatingMode _lastOperatingMode = static_cast<OperatingMode>(255);
  AlarmCode _lastAlarm = static_cast<AlarmCode>(255);
  RunState _lastRunState = static_cast<RunState>(255);
  UiMode _lastUiMode = static_cast<UiMode>(255);

  char _lastStageName[64] = {0};
  char _lastTempText[16] = {0};
  char _lastInfoText[32] = {0};
  char _lastMenuTitle[28] = {0};
  bool _lastMenuEditing = false;
  uint8_t _lastMenuItemCount = 0;
  uint8_t _lastMenuSelectedIndex = 0xFF;
  MenuRenderItem _lastMenuItems[10] = {};
};
