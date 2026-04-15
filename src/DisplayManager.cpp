#include "DisplayManager.h"

#include <cmath>
#include <cstring>

namespace {
constexpr int kCx = 120;
constexpr int kCy = 120;
constexpr int kRingOuter = 108;
constexpr int kRingInner = 96;
constexpr int kRingStart = -90;
constexpr int kRingSweep = 360;
constexpr int kIconCenterY = 110;

constexpr int kPillX = 40;
constexpr int kPillY = 42;
constexpr int kPillW = 160;
constexpr int kPillH = 24;
constexpr int kPillCenterX = kPillX + (kPillW / 2);
constexpr int kStageTextX = 64;
constexpr int kStageTextY = 42;
constexpr int kStageTextW = 112;
constexpr int kStageTextH = 26;

constexpr int kTempBoxX = 52;
constexpr int kTempBoxY = 74;
constexpr int kTempBoxW = 136;
constexpr int kTempBoxH = 56;

constexpr int kTargetRowX = 34;
constexpr int kTargetRowY = 138;
constexpr int kTargetRowW = 172;
constexpr int kTargetRowH = 24;

constexpr int kInfoRowX = 54;
constexpr int kInfoRowY = 172;
constexpr int kInfoRowW = 132;
constexpr int kInfoRowH = 22;

constexpr int kWifiHitX = 20;
constexpr int kWifiHitY = 84;
constexpr int kWifiHitW = 40;
constexpr int kWifiHitH = 36;

constexpr uint16_t BG = 0x0000;
constexpr uint16_t FG = 0xFFFF;
constexpr uint16_t FG_MUTED = 0xBDF7;
constexpr uint16_t OUTLINE_SOFT = 0x39E7;
constexpr uint16_t RING = 0x051D;
constexpr uint16_t RING_DIM = 0x18C3;

static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
}

void DisplayManager::begin() {
  drawStaticUi();
  invalidateAll();
}

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
  _lastRingSweep = -1;
  _lastRingMode = 0;

  _lastTempC = NAN;
  _lastSetpointC = NAN;
  _lastHeatOn = false;
  _lastWifiConnected = false;
  _lastMqttConnected = false;
  _lastAlarm = static_cast<AlarmCode>(255);
  _lastRunState = static_cast<RunState>(255);
  _lastUiMode = static_cast<UiMode>(255);

  _lastStageName[0] = '\0';
  _lastTempText[0] = '\0';
  _lastInfoText[0] = '\0';
}

void DisplayManager::requestImmediateUi() { _lastUiServiceMs = 0; }

bool DisplayManager::wasSettingsTouched() {
  const bool touched = _settingsTouched;
  _settingsTouched = false;
  return touched;
}

bool DisplayManager::wasAlarmPillTouched() {
  const bool touched = _alarmPillTouched;
  _alarmPillTouched = false;
  return touched;
}

String DisplayManager::formatTime(uint32_t sec) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02lu:%02lu", sec / 60UL, sec % 60UL);
  return String(buf);
}

String DisplayManager::formatMinutes(uint32_t minutes) {
  char buf[16];
  if (minutes >= 60) {
    snprintf(buf, sizeof(buf), "%luh %02lum", minutes / 60UL, minutes % 60UL);
  } else {
    snprintf(buf, sizeof(buf), "%lum", minutes);
  }
  return String(buf);
}

bool DisplayManager::hitRectPressed(int x, int y, int w, int h) {
  if (M5Dial.Touch.getCount() == 0) return false;
  const auto t = M5Dial.Touch.getDetail(0);
  if (!t.wasPressed()) return false;
  return (t.x >= x && t.x < (x + w) && t.y >= y && t.y < (y + h));
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

void DisplayManager::drawSettingsIcon(int x, int y, uint16_t color) {
  auto& d = M5Dial.Display;
  d.drawCircle(x, y, 8, color);
  d.drawCircle(x, y, 3, color);
  for (int i = 0; i < 8; ++i) {
    const float angle = (6.2831853f * i) / 8.0f;
    const int sx = x + static_cast<int>(cosf(angle) * 11.0f);
    const int sy = y + static_cast<int>(sinf(angle) * 11.0f);
    d.fillCircle(sx, sy, 1, color);
  }
}

void DisplayManager::drawStaticUi() {
  auto& d = M5Dial.Display;
  d.fillScreen(BG);

  d.fillCircle(kCx, kCy, 115, rgb(9, 9, 10));
  d.drawCircle(kCx, kCy, 115, rgb(24, 24, 24));
  d.drawCircle(kCx, kCy, 114, rgb(32, 32, 32));
  d.drawCircle(kCx, kCy, kRingOuter, OUTLINE_SOFT);
  d.drawCircle(kCx, kCy, kRingInner, OUTLINE_SOFT);

  // Shine accent: tiny top highlight for premium look.
  d.drawArc(kCx, kCy, 115, 113, 225, 315, rgb(70, 70, 78));

  d.fillRect(25, 34, 190, 40, BG);
  d.fillRect(30, 66, 180, 70, BG);
  d.fillRect(kTargetRowX, kTargetRowY, kTargetRowW, kTargetRowH, BG);
  d.fillRect(kInfoRowX, kInfoRowY, kInfoRowW, kInfoRowH, BG);

  d.fillRect(kWifiHitX, kWifiHitY, kWifiHitW, kWifiHitH, BG);
  drawSettingsIcon(42, kIconCenterY, OUTLINE_SOFT);

  _staticDrawn = true;
}

void DisplayManager::drawRing(float progress, bool timerStarted, RunState runState, uint32_t remainingSec, bool force) {
  const uint32_t remainingKey = timerStarted ? remainingSec : UINT32_MAX - 1;
  const bool runStateChanged = (_lastRunState != runState);
  const bool timerStateChanged = (_lastStageTimerStarted != timerStarted);
  const bool secondChanged = (_lastRingRemainingSec != remainingKey);

  if (!force && !_ringDirty && !runStateChanged && !timerStateChanged && !secondChanged) return;

  auto& d = M5Dial.Display;
  const bool ringEnabled = (runState == RunState::Running || runState == RunState::Paused || runState == RunState::Complete);
  const bool dimMode = ringEnabled && !timerStarted && runState != RunState::Complete;
  const bool activeMode = ringEnabled && !dimMode;
  const uint8_t ringMode = dimMode ? 1 : (activeMode ? 2 : 0);
  const float clamped = constrain(progress, 0.0f, 1.0f);
  const int sweep = activeMode ? int(kRingSweep * clamped + 0.5f) : 0;
  const bool needFullRedraw = force || _ringDirty || runStateChanged || timerStateChanged || (_lastRingMode != ringMode);

  if (needFullRedraw) {
    d.fillArc(kCx, kCy, kRingOuter, kRingInner, 0, 360, BG);
    d.drawArc(kCx, kCy, kRingOuter, kRingInner, 0, 360, rgb(38, 38, 38));

    if (dimMode) {
      d.fillArc(kCx, kCy, kRingOuter, kRingInner, kRingStart, kRingStart + kRingSweep, RING_DIM);
    } else if (activeMode && sweep > 0) {
      d.fillArc(kCx, kCy, kRingOuter, kRingInner, kRingStart, kRingStart + sweep, RING);
      d.drawArc(kCx, kCy, kRingOuter, kRingOuter - 1, kRingStart, kRingStart + sweep, rgb(145, 205, 255));
    }
  } else if (activeMode && secondChanged && _lastRingSweep >= 0 && sweep != _lastRingSweep) {
    const int oldSweep = _lastRingSweep;
    if (sweep < oldSweep) {
      const int eraseStart = kRingStart + sweep;
      const int eraseEnd = kRingStart + oldSweep;
      d.fillArc(kCx, kCy, kRingOuter, kRingInner, eraseStart, eraseEnd, BG);
      d.drawArc(kCx, kCy, kRingOuter, kRingInner, eraseStart, eraseEnd, rgb(38, 38, 38));
      d.drawArc(kCx, kCy, kRingOuter, kRingOuter - 1, kRingStart, kRingStart + sweep, rgb(145, 205, 255));
    } else {
      const int growStart = kRingStart + oldSweep;
      const int growEnd = kRingStart + sweep;
      d.fillArc(kCx, kCy, kRingOuter, kRingInner, growStart, growEnd, RING);
      d.drawArc(kCx, kCy, kRingOuter, kRingOuter - 1, growStart, growEnd, rgb(145, 205, 255));
    }
  }

  _ringDirty = false;
  _lastStageTimerStarted = timerStarted;
  _lastRingRemainingSec = remainingKey;
  _lastRingSweep = sweep;
  _lastRingMode = ringMode;
}

void DisplayManager::drawStagePill(const RuntimeState& rt, const ProcessStage* stage, bool force) {
  const char* text = nullptr;
  if (rt.activeAlarm != AlarmCode::None) text = rt.alarmText;
  else if (rt.uiMode == UiMode::SetpointAdjust) text = "SET TEMP";
  else if (rt.uiMode == UiMode::StageTimeAdjust) text = "SET TIME";
  else if (rt.uiMode == UiMode::SettingsAdjust) text = "SETTINGS";
  else if (rt.runState == RunState::Complete) text = "COMPLETE";
  else if (rt.runState == RunState::Paused) text = "PAUSED";
  else if (stage) text = stage->name;
  else text = "IDLE";

  if (!force && strcmp(_lastStageName, text) == 0 && _lastAlarm == rt.activeAlarm && _lastRunState == rt.runState && _lastUiMode == rt.uiMode) return;

  auto& d = M5Dial.Display;
  d.fillRect(kStageTextX, kStageTextY, kStageTextW, kStageTextH, BG);

  d.setFont(&fonts::Font2);
  d.setTextDatum(top_center);
  if (rt.activeAlarm != AlarmCode::None) {
    d.setTextColor(FG_MUTED, BG);
    d.drawString(text, kPillCenterX, kPillY + 1);
    d.setFont(&fonts::Font0);
    d.setTextColor(GOLD, BG);
    d.drawString("TAP TO ACK", kPillCenterX, kPillY + 14);
  } else {
    d.setTextColor(FG_MUTED, BG);
    d.drawString(text, kPillCenterX, kPillY + 5);
  }

  strlcpy(_lastStageName, text, sizeof(_lastStageName));
  _lastAlarm = rt.activeAlarm;
  _lastRunState = rt.runState;
  _lastUiMode = rt.uiMode;
}

void DisplayManager::drawCenterTemp(const RuntimeState& rt, uint32_t now, bool force) {
  if (!force && (now - _lastTempDrawMs < Config::UI_TEMP_MS)) return;
  if (!force && !isnan(_lastTempC) && !isnan(rt.currentTempC) && fabsf(_lastTempC - rt.currentTempC) < 0.05f) return;

  auto& d = M5Dial.Display;

  char tempBuf[16];
  if (isnan(rt.currentTempC)) snprintf(tempBuf, sizeof(tempBuf), "--.-");
  else snprintf(tempBuf, sizeof(tempBuf), "%.1f", rt.currentTempC);

  d.setFont(&fonts::Font7);
  const int newWidth = d.textWidth(tempBuf);
  const int oldWidth = (_lastTempText[0] != '\0') ? d.textWidth(_lastTempText) : -1;
  const int lenNew = strlen(tempBuf);
  const int lenOld = strlen(_lastTempText);

  const bool fullRedraw = force || _lastTempText[0] == '\0' || oldWidth != newWidth || lenOld != lenNew;
  if (fullRedraw) {
    d.fillRect(kTempBoxX, kTempBoxY, kTempBoxW, kTempBoxH, BG);

    d.setTextDatum(middle_center);
    d.setTextColor(rgb(90, 70, 28), BG);
    d.drawString(tempBuf, 116, 104);
    d.setTextColor(FG, BG);
    d.drawString(tempBuf, 114, 102);

    d.setFont(&fonts::Font4);
    d.setTextColor(GOLD, BG);
    d.drawString("C", 180, 86);
  } else {
    const int startX = 114 - (newWidth / 2);
    const int fontHeight = d.fontHeight();
    const int y = 102 - (fontHeight / 2);

    d.setTextDatum(top_left);
    for (int i = 0; i < lenNew; ++i) {
      if (_lastTempText[i] == tempBuf[i]) continue;

      char oldCh[2] = {_lastTempText[i], '\0'};
      char newCh[2] = {tempBuf[i], '\0'};
      char prefix[16] = {0};
      if (i > 0) {
        memcpy(prefix, tempBuf, i);
        prefix[i] = '\0';
      }

      const int charX = startX + d.textWidth(prefix);
      const int oldW = d.textWidth(oldCh);
      const int newW = d.textWidth(newCh);
      d.fillRect(charX - 1, y - 1, max(oldW, newW) + 2, fontHeight + 2, BG);

      d.setTextColor(rgb(90, 70, 28), BG);
      d.drawString(newCh, charX + 2, y + 2);
      d.setTextColor(FG, BG);
      d.drawString(newCh, charX, y);
    }
  }

  strlcpy(_lastTempText, tempBuf, sizeof(_lastTempText));
  _lastTempC = rt.currentTempC;
  _lastTempDrawMs = now;
}

void DisplayManager::drawTargetRow(const RuntimeState& rt, bool force) {
  if (rt.uiMode == UiMode::SettingsAdjust) {
    auto& d = M5Dial.Display;
    d.fillRect(kTargetRowX, kTargetRowY, kTargetRowW, kTargetRowH, BG);
    d.setTextDatum(middle_center);
    d.setFont(&fonts::Font2);
    d.setTextColor(GOLD, BG);
    d.drawString(rt.settingsLabel[0] ? rt.settingsLabel : "SETTINGS", 120, kTargetRowY + (kTargetRowH / 2));
    _lastSetpointC = rt.currentSetpointC;
    return;
  }

  if (!force && fabsf(_lastSetpointC - rt.currentSetpointC) < 0.05f) return;

  auto& d = M5Dial.Display;
  d.fillRect(kTargetRowX, kTargetRowY, kTargetRowW, kTargetRowH, BG);

  char spBuf[20];
  snprintf(spBuf, sizeof(spBuf), "Setpoint: %.1fC", rt.currentSetpointC);

  d.setTextDatum(middle_center);
  d.setFont(&fonts::Font2);
  d.setTextColor(GOLD, BG);
  d.drawString(spBuf, 120, kTargetRowY + (kTargetRowH / 2));

  _lastSetpointC = rt.currentSetpointC;
}

void DisplayManager::drawInfoRow(const RuntimeState& rt, uint32_t remainingSec, bool force) {
  char infoBuf[32];
  if (rt.uiMode == UiMode::SettingsAdjust) {
    snprintf(infoBuf, sizeof(infoBuf), "%s", rt.settingsValue[0] ? rt.settingsValue : "--");
  } else if (rt.uiMode == UiMode::StageTimeAdjust) {
    snprintf(infoBuf, sizeof(infoBuf), "%s", formatMinutes(rt.activeStageMinutes).c_str());
  } else if (rt.activeStageMinutes == 0 && (rt.runState == RunState::Running || rt.runState == RunState::Paused)) {
    snprintf(infoBuf, sizeof(infoBuf), "INF");
  } else {
    snprintf(infoBuf, sizeof(infoBuf), "%s", formatTime(remainingSec).c_str());
  }

  if (!force && strcmp(_lastInfoText, infoBuf) == 0 && _lastUiMode == rt.uiMode) return;

  auto& d = M5Dial.Display;
  d.fillRect(kInfoRowX, kInfoRowY, kInfoRowW, kInfoRowH, BG);
  d.setTextDatum(top_center);
  d.setFont(&fonts::Font4);
  d.setTextColor(FG, BG);
  d.drawString(infoBuf, 120, 176);

  strlcpy(_lastInfoText, infoBuf, sizeof(_lastInfoText));
  _lastRemainingSec = remainingSec;
}

void DisplayManager::drawHeatIcon(const RuntimeState& rt, bool force) {
  const bool on = rt.heatOn;
  if (!force && _lastHeatOn == on) return;

  auto& d = M5Dial.Display;
  d.fillRect(184, 84, 28, 36, BG);
  drawFireGlyph(198, kIconCenterY, on ? RED : OUTLINE_SOFT);

  _lastHeatOn = on;
}

void DisplayManager::drawWifiIcon(const RuntimeState& rt, bool force) {
  if (!force && _lastWifiConnected == rt.wifiConnected && _lastMqttConnected == rt.mqttConnected) return;

  auto& d = M5Dial.Display;
  d.fillRect(28, 84, 28, 28, BG);

  uint16_t color = OUTLINE_SOFT;
  if (rt.wifiConnected && rt.mqttConnected) color = BLUE;
  else if (rt.wifiConnected) color = GOLD;

  drawWifiGlyph(42, kIconCenterY, color);

  _lastWifiConnected = rt.wifiConnected;
  _lastMqttConnected = rt.mqttConnected;
}

void DisplayManager::draw(const PersistentConfig&, const RuntimeState& rt, const ProcessStage* stage, uint32_t remainingSec) {
  const uint32_t now = millis();
  if (!_staticDrawn || _forceFull) drawStaticUi();
  if (!_forceFull && (now - _lastUiServiceMs < Config::UI_SERVICE_MS)) return;
  _lastUiServiceMs = now;

  _settingsTouched = hitRectPressed(kWifiHitX, kWifiHitY, kWifiHitW, kWifiHitH);
  _alarmPillTouched = hitRectPressed(kPillX, kPillY, kPillW, kPillH);

  const uint32_t totalSec = max<uint32_t>(1, rt.activeStageMinutes * 60UL);
  const float progress = rt.stageTimerStarted
                             ? static_cast<float>(remainingSec) / static_cast<float>(totalSec)
                             : 1.0f;

  const bool ringTick = (now - _lastRingDrawMs >= Config::UI_RING_MS);
  const bool ringNeedsUpdate = _forceFull || _ringDirty || ringTick || (_lastRunState != rt.runState) ||
                               (_lastStageTimerStarted != rt.stageTimerStarted) ||
                               (_lastRingRemainingSec != (rt.stageTimerStarted ? remainingSec : UINT32_MAX - 1));

  M5Dial.Display.startWrite();
  if (ringNeedsUpdate) {
    drawRing(progress, rt.stageTimerStarted, rt.runState, remainingSec, _forceFull);
    _lastRingDrawMs = now;
  }

  drawStagePill(rt, stage, _forceFull);
  drawCenterTemp(rt, now, _forceFull);
  drawTargetRow(rt, _forceFull);
  drawInfoRow(rt, remainingSec, _forceFull);
  drawHeatIcon(rt, _forceFull);
  drawWifiIcon(rt, _forceFull);
  M5Dial.Display.endWrite();

  _forceFull = false;
}
