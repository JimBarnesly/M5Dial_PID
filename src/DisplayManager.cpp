#include "DisplayManager.h"
#include "Config.h"
#include <cmath>
#include <cstring>
#include "DisplayManager.h"
#include <M5Dial.h>
#include <math.h>
namespace {
  constexpr int kCx = 120;
  constexpr int kCy = 120;
  constexpr int kRingOuter = 108;
  constexpr int kRingInner = 96;

  static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  }

  constexpr uint16_t BG           = 0x0000;
  constexpr uint16_t FG           = 0xFFFF;
  constexpr uint16_t FG_MUTED     = 0xBDF7;
  //constexpr uint16_t GOLD         = 0xFEA0;
  //constexpr uint16_t RED          = 0xF800;
  constexpr uint16_t RED_DARK     = 0x8000;
  //constexpr uint16_t BLUE         = 0x051D;
  constexpr uint16_t SURFACE_LOW  = 0x18C3;
  constexpr uint16_t OUTLINE_SOFT = 0x39E7;

  constexpr int kRingStart = -90;
  constexpr int kRingSweep = 360;

  constexpr uint16_t RING     = 0x051D; //rgb(0, 122, 255);
  constexpr uint16_t RING_DIM = 0x18C3; //rgb(38, 38, 38);

  constexpr int iconCenterY = 110;
}

void DisplayManager::begin() { drawStaticUi(); invalidateAll(); }

void DisplayManager::invalidateAll() {
  _staticDrawn = false;
  _forceFull = true;
  _settingsTouched = false;
  _alarmPillTouched = false;
  _ringDirty = true;

  _lastUiServiceMs = 0;
  _lastTempDrawMs = 0;
  _lastRingDrawMs = 0;
  _lastRingRemainingSec = UINT32_MAX;
  _lastStageTimerStarted = false;
  _lastRemainingSec = UINT32_MAX;

  _lastTempC = NAN;
  _lastSetpointC = NAN;
  _lastHeatOn = false;
  _lastWifiConnected = false;
  _lastMqttConnected = false;
  _lastAlarm = static_cast<AlarmCode>(255);
  _lastRunState = static_cast<RunState>(255);
  _lastUiMode = static_cast<UiMode>(255);

  _lastStageName[0] = '\0';
  _lastInfoText[0] = '\0';
}

void DisplayManager::requestImmediateUi() { _lastUiServiceMs = 0; }

String DisplayManager::formatTime(uint32_t sec) {
  char buf[8]; snprintf(buf, sizeof(buf), "%02lu:%02lu", sec / 60UL, sec % 60UL); return String(buf);
}
String DisplayManager::formatMinutes(uint32_t minutes) {
  char buf[16];
  if (minutes >= 60) snprintf(buf, sizeof(buf), "%luh %02lum", minutes/60UL, minutes%60UL);
  else snprintf(buf, sizeof(buf), "%lum", minutes);
  return String(buf);
}

void DisplayManager::drawWifiGlyph(int x, int y, uint16_t color) {
  auto& d = M5Dial.Display;
  d.drawArc(x, y, 14, 14, 220, 320, color);
  d.drawArc(x, y, 10, 10, 225, 315, color);
  d.drawArc(x, y, 6, 6, 230, 310, color);
  d.fillCircle(x, y + 6, 2, color);
}
void DisplayManager::drawFireGlyph(int x, int y, uint16_t color) {
  auto& d = M5Dial.Display;
  d.fillTriangle(x, y - 11, x - 7, y + 4, x + 2, y + 8, color);
  d.fillTriangle(x + 3, y - 4, x - 2, y + 9, x + 9, y + 4, color);
  d.fillCircle(x - 1, y + 4, 6, color);
  d.fillCircle(x + 4, y + 5, 4, BG);
  d.fillCircle(x + 1, y + 2, 2, BG);
}

void DisplayManager::drawStaticUi() {
  auto& d = M5Dial.Display;
  d.fillScreen(BG);
  d.fillCircle(kCx, kCy, 115, rgb(9, 9, 10));
  d.drawCircle(kCx, kCy, 115, rgb(24, 24, 24));
  d.drawCircle(kCx, kCy, 114, rgb(32, 32, 32));
  d.drawCircle(kCx, kCy, kRingOuter, OUTLINE_SOFT);
  d.drawCircle(kCx, kCy, kRingInner, OUTLINE_SOFT);
  d.fillRect(25, 34, 190, 40, BG);
  d.fillRect(30, 66, 180, 70, BG);
  d.fillRect(34, 136, 172, 30, BG);
  d.fillRect(60, 172, 120, 22, BG);
  _staticDrawn = true;
}

void DisplayManager::drawRing(float progress, bool timerStarted, RunState runState, bool force) {
  const uint32_t remainingKey = timerStarted ? _lastRemainingSec : UINT32_MAX - 1;
  if (!force && !_ringDirty && _lastRingRemainingSec == remainingKey && _lastStageTimerStarted == timerStarted && _lastRunState == runState) return;
  auto& d = M5Dial.Display;
  d.fillArc(kCx, kCy, kRingOuter, kRingInner, 0, 360, BG);
  d.drawArc(kCx, kCy, kRingOuter, kRingInner, 0, 360, rgb(38, 38, 38));
  if (runState == RunState::Running || runState == RunState::Paused || runState == RunState::Complete) {
    if (!timerStarted && runState != RunState::Complete) {
      d.fillArc(kCx, kCy, kRingOuter, kRingInner, kRingStart, kRingStart + kRingSweep, RING_DIM);
    } else {
      const float clamped = constrain(progress, 0.0f, 1.0f);
      const int sweep = int(kRingSweep * clamped + 0.5f);
      if (sweep > 0) d.fillArc(kCx, kCy, kRingOuter, kRingInner, kRingStart, kRingStart + sweep, RING);
    }
  }
  _ringDirty = false; _lastStageTimerStarted = timerStarted; _lastRingRemainingSec = remainingKey;
}

void DisplayManager::drawStagePill(const RuntimeState& rt, const BrewStage* stage, bool force) {
  const char* text = nullptr;
  if (rt.activeAlarm != AlarmCode::None) text = rt.alarmText;
  else if (rt.uiMode == UiMode::SetpointAdjust) text = "SET TEMP";
  else if (rt.uiMode == UiMode::StageTimeAdjust) text = "SET TIME";
  else if (rt.runState == RunState::Complete) text = "COMPLETE";
  else if (rt.runState == RunState::Paused) text = "PAUSED";
  else if (stage) text = stage->name;
  else text = "IDLE";

  if (!force && strcmp(_lastStageName, text) == 0 && _lastAlarm == rt.activeAlarm && _lastRunState == rt.runState && _lastUiMode == rt.uiMode) return;

  constexpr int screenW = 240, pillW = 160, pillH = 24, pillR = 12, clearY = 38, clearH = 34, pillY = 42;
  const int pillX = (screenW - pillW) / 2; const int pillCenterX = screenW / 2; const int pillCenterY = pillY + (pillH/2);
  auto& d = M5Dial.Display;
  d.fillRect(pillX, clearY, pillW, clearH, BG);
  d.fillRoundRect(pillX, pillY, pillW, pillH, pillR, SURFACE_LOW);
  d.drawRoundRect(pillX, pillY, pillW, pillH, pillR, rgb(30,30,30));
  d.setFont(&fonts::Font2); d.setTextColor(FG_MUTED, SURFACE_LOW);
  if (rt.activeAlarm != AlarmCode::None) {
    d.setTextDatum(top_center); d.drawString(text, pillCenterX, pillY + 2);
    d.setFont(&fonts::Font0); d.setTextColor(GOLD, SURFACE_LOW); d.drawString("TAP TO ACK", pillCenterX, pillY + 14);
    d.setFont(&fonts::Font2); d.setTextColor(FG_MUTED, SURFACE_LOW);
  } else {
    d.setTextDatum(middle_center); d.drawString(text, pillCenterX, pillCenterY);
  }
  const int textWidth = d.textWidth(text); const int textLeftX = pillCenterX - (textWidth/2);
  d.fillCircle(textLeftX - 8, pillCenterY, 4, rt.activeAlarm != AlarmCode::None ? RED : RED_DARK);
  strlcpy(_lastStageName, text, sizeof(_lastStageName)); _lastAlarm = rt.activeAlarm; _lastRunState = rt.runState; _lastUiMode = rt.uiMode;
}

void DisplayManager::drawCenterTemp(const RuntimeState& rt, uint32_t now, bool force) {
  if (!force && (now - _lastTempDrawMs < Config::UI_TEMP_MS)) return;
  if (!force && !isnan(_lastTempC) && !isnan(rt.currentTempC) && fabsf(_lastTempC - rt.currentTempC) < 0.05f) return;

  auto& d = M5Dial.Display;

  // Smaller clear area fully inside the ring
  constexpr int boxX = 52;
  constexpr int boxY = 74;
  constexpr int boxW = 136;
  constexpr int boxH = 56;

  d.fillRect(boxX, boxY, boxW, boxH, BG);

  char tempBuf[16];
  if (isnan(rt.currentTempC)) {
    snprintf(tempBuf, sizeof(tempBuf), "--.-");
  } else {
    snprintf(tempBuf, sizeof(tempBuf), "%.1f", rt.currentTempC);
  }

  d.setTextDatum(middle_center);
  d.setFont(&fonts::Font7);

  // main text
  d.setTextColor(rgb(90, 70, 28), BG);
  d.drawString(tempBuf, 116, 104);

  d.setTextColor(FG, BG);
  d.drawString(tempBuf, 114, 102);

  d.setFont(&fonts::Font4);
  d.setTextColor(GOLD, BG);
  d.drawString("C", 180, 86);

  _lastTempC = rt.currentTempC;
  _lastTempDrawMs = now;
}

void DisplayManager::drawTargetRow(const RuntimeState& rt, bool force) {
  if (!force && fabsf(_lastSetpointC - rt.currentSetpointC) < 0.05f) return;

  auto& d = M5Dial.Display;

  constexpr int rowX = 34;
  constexpr int rowY = 138;
  constexpr int rowW = 172;
  constexpr int rowH = 24;

  d.fillRect(rowX, rowY, rowW, rowH, BG);

  char spBuf[20];
  snprintf(spBuf, sizeof(spBuf), "Setpoint: %.1fC", rt.currentSetpointC);

  d.setTextDatum(middle_center);
  d.setFont(&fonts::Font2);
  d.setTextColor(GOLD, BG);
  d.drawString(spBuf, 120, rowY + rowH / 2);

  _lastSetpointC = rt.currentSetpointC;
}

void DisplayManager::drawInfoRow(const RuntimeState& rt, uint32_t remainingSec, bool force) {
  if (!force && _lastRemainingSec == remainingSec && _lastUiMode == rt.uiMode) return;
  auto& d = M5Dial.Display;
  d.fillRect(54, 172, 132, 22, BG);
  d.setTextDatum(top_center); d.setFont(&fonts::Font4); d.setTextColor(FG, BG);
  if (rt.uiMode == UiMode::StageTimeAdjust) d.drawString(formatMinutes(rt.activeStageMinutes), 120, 176);
  else d.drawString(formatTime(remainingSec), 120, 176);
  _lastRemainingSec = remainingSec;
}

void DisplayManager::drawHeatIcon(const RuntimeState& rt, bool force) {
  const bool on = rt.heatOn;
  if (!force && _lastHeatOn == on) return;

  auto& d = M5Dial.Display;
  d.fillRect(184, 84, 28, 36, BG);
  drawFireGlyph(198, iconCenterY, on ? RED : OUTLINE_SOFT);

  _lastHeatOn = on;
}

void DisplayManager::drawWifiIcon(const RuntimeState& rt, bool force) {
  if (!force && _lastWifiConnected == rt.wifiConnected && _lastMqttConnected == rt.mqttConnected) return;

  auto& d = M5Dial.Display;
  d.fillRect(28, 84, 28, 28, BG);

  uint16_t color = OUTLINE_SOFT;
  if (rt.wifiConnected && rt.mqttConnected) color = BLUE;
  else if (rt.wifiConnected) color = GOLD;

  drawWifiGlyph(42, iconCenterY, color);

  _lastWifiConnected = rt.wifiConnected;
  _lastMqttConnected = rt.mqttConnected;
}

void DisplayManager::draw(const PersistentConfig&, const RuntimeState& rt, const BrewStage* stage, uint32_t remainingSec) {
  const uint32_t now = millis(); if (!_staticDrawn || _forceFull) drawStaticUi(); if (!_forceFull && now - _lastUiServiceMs < Config::UI_SERVICE_MS) return; _lastUiServiceMs = now;
  M5Dial.Display.startWrite();
  const uint32_t totalSec = max<uint32_t>(1, rt.activeStageMinutes * 60UL);
  const float progress = (rt.stageTimerStarted) ? static_cast<float>(remainingSec) / static_cast<float>(totalSec) : 1.0f;
  if (_forceFull || now - _lastRingDrawMs >= Config::UI_RING_MS) { _ringDirty = true; drawRing(progress, rt.stageTimerStarted, rt.runState, true); _lastRingDrawMs = now; }
  drawStagePill(rt, stage, _forceFull); drawCenterTemp(rt, now, _forceFull); drawTargetRow(rt, _forceFull); drawInfoRow(rt, remainingSec, _forceFull); drawHeatIcon(rt, _forceFull); drawWifiIcon(rt, _forceFull);
  _forceFull = false; M5Dial.Display.endWrite();
}
