#include "DisplayManager.h"
#include "Config.h"
#include <cmath>
#include <cstring>

namespace {
constexpr int kCx = 120;
constexpr int kCy = 120;
constexpr int kRingOuter = 110;
constexpr int kRingInner = 101;
constexpr int kRingStart = -90;
constexpr int kRingSweep = 360;
}

void DisplayManager::begin() {
  drawStaticUi();
  invalidateAll();
}

void DisplayManager::invalidateAll() {
  _forceFull = true;
  _ringDirty = true;
  _lastUiServiceMs = 0;
  _lastTempDrawMs = 0;
  _lastRingDrawMs = 0;
  _lastTempC = NAN;
  _lastSetpointC = NAN;
  _lastHeaterPct = -999.0f;
  _lastRemainingSec = UINT32_MAX;
  _lastWifiConnected = !_lastWifiConnected;
  _lastMqttConnected = !_lastMqttConnected;
  _lastHeatOn = !_lastHeatOn;
  _lastEditMode = !_lastEditMode;
  _lastAlarm = static_cast<AlarmCode>(255);
  _lastAlarmText[0] = '\0';
  _lastStageName[0] = '\0';
}


void DisplayManager::requestImmediateUi() {
  _lastUiServiceMs = 0;
}

String DisplayManager::formatTime(uint32_t sec) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02lu:%02lu", sec / 60, sec % 60);
  return String(buf);
}

void DisplayManager::drawSettingsIcon(int x, int y, uint16_t color) {
  auto& d = M5Dial.Display;
  d.drawCircle(x, y, 7, color);
  d.fillCircle(x, y, 2, color);
  for (int i = 0; i < 8; ++i) {
    float a = i * 45.0f * DEG_TO_RAD;
    int x1 = x + int(cosf(a) * 9.0f);
    int y1 = y + int(sinf(a) * 9.0f);
    int x2 = x + int(cosf(a) * 12.0f);
    int y2 = y + int(sinf(a) * 12.0f);
    d.drawLine(x1, y1, x2, y2, color);
  }
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

void DisplayManager::drawStopButton() {
  auto& d = M5Dial.Display;
  d.fillRoundRect(96, 188, 48, 52, 23, rgb(46, 34, 16));
  d.drawRoundRect(96, 188, 48, 52, 23, rgb(112, 78, 24));
  d.drawRoundRect(97, 189, 46, 50, 22, rgb(70, 50, 18));
  d.fillRect(113, 208, 14, 14, GOLD);
}

void DisplayManager::drawStaticUi() {
  auto& d = M5Dial.Display;
  d.startWrite();
  d.fillScreen(BG);

  d.fillCircle(kCx, kCy, 115, rgb(9, 9, 10));
  d.drawCircle(kCx, kCy, 115, rgb(24, 24, 24));
  d.drawCircle(kCx, kCy, 114, rgb(32, 32, 32));

  d.drawCircle(kCx, kCy, kRingOuter, OUTLINE_SOFT);
  d.drawCircle(kCx, kCy, kRingInner, OUTLINE_SOFT);

  //drawBrand(true);
  drawBottomControls(true);

  // Static labels around the information row.
  d.setTextDatum(top_left);
  d.setFont(&fonts::Font2);
  d.setTextColor(OUTLINE, BG);
  d.drawString("TARGET:", 77, 142);
  d.drawString("TIME", 76, 166);
  d.drawString("POWER", 140, 166);
  d.drawFastVLine(121, 176, 18, OUTLINE_SOFT);

  // Soft dark pads under dynamic zones.
  d.fillRoundRect(56, 136, 128, 28, 10, BG);
  d.fillRect(62, 176, 54, 20, BG);
  d.fillRect(126, 176, 52, 20, BG);
  d.fillRect(35, 68, 170, 68, BG);
  d.fillRect(188, 92, 26, 34, BG);

  _staticDrawn = true;
  d.endWrite();
}

/*void DisplayManager::drawBrand(bool force) {
  if (!force) return;
  auto& d = M5Dial.Display;
  d.fillRect(80, 18, 80, 14, BG);
  d.setTextDatum(top_center);
  d.setFont(&fonts::Font2);
  d.setTextColor(GOLD_DIM, BG);
  d.drawString("BREW_CORE", 120, 23);
}*/

void DisplayManager::drawBottomControls(bool force) {
  if (!force) return;
  auto& d = M5Dial.Display;
  d.fillRect(24, 184, 192, 54, BG);
  drawSettingsIcon(60, 213, OUTLINE_SOFT);
  drawStopButton();
  drawWifiGlyph(180, 214, OUTLINE_SOFT);
}

void DisplayManager::drawRing(float progress, bool force) {
  if (!force && !_ringDirty) return;
  auto& d = M5Dial.Display;

  d.fillArc(kCx, kCy, kRingOuter, kRingInner, kRingStart, kRingStart + kRingSweep, rgb(38, 38, 38));
  d.fillArc(kCx, kCy, kRingOuter, kRingInner, 0, 360, BG);
  d.drawArc(kCx, kCy, kRingOuter, kRingInner, kRingStart, kRingStart + kRingSweep, rgb(38, 38, 38));

  float clamped = constrain(progress, 0.0f, 1.0f);
  if (clamped > 0.0f) {
    const int total = int(kRingSweep * clamped);
    const int segs = 28;
    for (int i = 0; i < segs; ++i) {
      int a0 = kRingStart + (total * i) / segs;
      int a1 = kRingStart + (total * (i + 1)) / segs - 1;
      if (a1 < a0) continue;
      float t = float(i) / float(segs - 1);
      uint8_t r = uint8_t((1.0f - t) * 58 + t * 0);
      uint8_t g = uint8_t((1.0f - t) * 162 + t * 97);
      uint8_t b = 255;
      d.fillArc(kCx, kCy, kRingOuter, kRingInner, a0, a1, rgb(r, g, b));
    }
  }
  _ringDirty = false;
}

void DisplayManager::drawStagePill(const RuntimeState& rt, const BrewStage* stage, bool force) {
  const char* text = nullptr;
  if (rt.activeAlarm != AlarmCode::None) {
    text = rt.alarmText;
  } else if (stage) {
    text = stage->name;
  } else if (rt.runState == RunState::Complete) {
    text = "COMPLETE";
  } else if (rt.runState == RunState::Paused) {
    text = "PAUSED";
  } else {
    text = "IDLE";
  }

  if (!force && strcmp(_lastStageName, text) == 0 && _lastAlarm == rt.activeAlarm) return;

  auto& d = M5Dial.Display;
  d.fillRect(68, 34, 104, 26, BG);
  d.fillRoundRect(68, 36, 104, 22, 11, SURFACE_LOW);
  d.drawRoundRect(68, 36, 104, 22, 11, rgb(30, 30, 30));
  d.fillCircle(84, 47, 4, rt.activeAlarm != AlarmCode::None ? RED : RED_DARK);
  d.setTextDatum(top_left);
  d.setFont(&fonts::Font2);
  d.setTextColor(FG_MUTED, SURFACE_LOW);
  d.drawString(text, 98, 40);

  strlcpy(_lastStageName, text, sizeof(_lastStageName));
  _lastAlarm = rt.activeAlarm;
}

void DisplayManager::drawCenterTemp(const RuntimeState& rt, uint32_t now, bool force) {
  if (!force && now - _lastTempDrawMs < Config::UI_TEMP_TEXT_MS) return;
  if (!force && !isnan(rt.currentTempC) && !isnan(_lastTempC) && fabsf(rt.currentTempC - _lastTempC) < 0.05f) return;
  if (!force && isnan(rt.currentTempC) == isnan(_lastTempC) && isnan(rt.currentTempC)) return;

  auto& d = M5Dial.Display;
  d.fillRect(34, 66, 174, 66, BG);
  d.fillRect(183, 91, 30, 34, BG);
  d.setTextDatum(middle_center);
  d.setFont(&fonts::Font7);

  char tempBuf[16];
  if (isnan(rt.currentTempC)) snprintf(tempBuf, sizeof(tempBuf), "--.-");
  else snprintf(tempBuf, sizeof(tempBuf), "%.1f", rt.currentTempC);

  d.setTextColor(rgb(90, 70, 28), BG);
  d.drawString(tempBuf, 111, 104);
  d.setTextColor(rgb(130, 98, 30), BG);
  d.drawString(tempBuf, 109, 102);
  d.setTextColor(FG, BG);
  d.drawString(tempBuf, 108, 100);

  d.setFont(&fonts::Font4);
  d.setTextColor(GOLD, BG);
  d.drawString("C", 194, 104);

  _lastTempC = rt.currentTempC;
  _lastTempDrawMs = now;
}

void DisplayManager::drawTargetRow(const RuntimeState& rt, bool force) {
  if (!force && !std::isnan(_lastSetpointC) && fabsf(rt.currentSetpointC - _lastSetpointC) < 0.001f && _lastEditMode == rt.editSetpointMode) return;

  auto& d = M5Dial.Display;
  d.fillRect(18, 134, 204, 32, BG);

  if (rt.editSetpointMode) {
    d.fillRoundRect(18, 134, 204, 32, 16, SURFACE_LOW);
    d.drawRoundRect(18, 134, 204, 32, 16, GOLD_DIM);
  }

  d.setTextDatum(top_left);
  d.setFont(&fonts::Font2);
  d.setTextColor(FG_MUTED, BG);
  d.drawString(rt.editSetpointMode ? "SETPOINT" : "TARGET", 28, 142);

  d.setFont(&fonts::Font4);
  d.setTextColor(rt.editSetpointMode ? FG : GOLD, BG);

  char spBuf[16];
  snprintf(spBuf, sizeof(spBuf), "%.1fC", rt.currentSetpointC);
  d.drawString(spBuf, 116, 140);

  d.setFont(&fonts::Font0);
  d.setTextColor(rt.editSetpointMode ? GOLD : OUTLINE, BG);
  //d.drawString(rt.editSetpointMode ? "TURN TO ADJUST" : "PRESS TO EDIT", 28, 156);

  _lastSetpointC = rt.currentSetpointC;
  _lastEditMode = rt.editSetpointMode;
}

void DisplayManager::drawInfoRow(const RuntimeState& rt, uint32_t remainingSec, bool force) {
  if (!force && _lastRemainingSec == remainingSec && fabsf(_lastHeaterPct - rt.heaterOutputPct) < 0.5f) return;

  auto& d = M5Dial.Display;
  d.fillRect(67, 176, 49, 20, BG);
  d.fillRect(140, 176, 42, 20, BG);
  d.setTextDatum(top_left);
  d.setFont(&fonts::Font4);
  d.setTextColor(FG, BG);
  d.drawString(formatTime(remainingSec), 68, 178);

  d.setTextColor(BLUE, BG);
  char outBuf[12];
  snprintf(outBuf, sizeof(outBuf), "%.0f%%", rt.heaterOutputPct);
  d.drawString(outBuf, 142, 178);

  _lastRemainingSec = remainingSec;
  _lastHeaterPct = rt.heaterOutputPct;
}

void DisplayManager::drawHeatIcon(const RuntimeState& rt, bool force) {
  const bool on = rt.heaterOutputPct > 1.0f && rt.heatingEnabled && rt.activeAlarm == AlarmCode::None;
  if (!force && _lastHeatOn == on) return;

  auto& d = M5Dial.Display;
  d.fillRect(186, 89, 28, 36, BG);
  drawFireGlyph(199, 106, on ? RED : OUTLINE_SOFT);
  _lastHeatOn = on;
}

void DisplayManager::drawWifiIcon(const RuntimeState& rt, bool force) {
  if (!force && _lastWifiConnected == rt.wifiConnected && _lastMqttConnected == rt.mqttConnected) return;

  auto& d = M5Dial.Display;
  d.fillRect(166, 199, 28, 26, BG);
  uint16_t color = OUTLINE_SOFT;
  if (rt.wifiConnected && rt.mqttConnected) color = BLUE;
  else if (rt.wifiConnected) color = GOLD;
  drawWifiGlyph(180, 214, color);

  _lastWifiConnected = rt.wifiConnected;
  _lastMqttConnected = rt.mqttConnected;
}

void DisplayManager::drawAlarmOverlay(const RuntimeState& rt, bool force) {
  if (!force && _lastAlarm == rt.activeAlarm && strcmp(_lastAlarmText, rt.alarmText) == 0) return;

  auto& d = M5Dial.Display;
  d.fillRect(18, 2, 204, 14, BG);
  if (rt.activeAlarm != AlarmCode::None) {
    d.fillRoundRect(28, 4, 184, 12, 6, RED_DARK);
    d.setTextDatum(top_center);
    d.setFont(&fonts::Font0);
    d.setTextColor(FG, RED_DARK);
    d.drawString(rt.alarmText, 120, 6);
  }

  _lastAlarm = rt.activeAlarm;
  strlcpy(_lastAlarmText, rt.alarmText, sizeof(_lastAlarmText));
}

void DisplayManager::draw(const PersistentConfig&, const RuntimeState& rt, const BrewStage* stage, uint32_t remainingSec) {
  const uint32_t now = millis();
  if (!_staticDrawn) drawStaticUi();
  if (!_forceFull && now - _lastUiServiceMs < Config::UI_SERVICE_MS) return;
  _lastUiServiceMs = now;

  M5Dial.Display.startWrite();

  const float progress = (stage && stage->holdSeconds > 0)
                           ? (rt.stageTimerStarted
                                ? static_cast<float>(remainingSec) / static_cast<float>(stage->holdSeconds)
                                : 1.0f)
                           : 0.0f;

  if (_forceFull || now - _lastRingDrawMs >= Config::UI_RING_MS) {
    _ringDirty = true;
    drawRing(progress, true);
    _lastRingDrawMs = now;
  }

  if (_forceFull) {
    drawStaticUi();
  }

  drawStagePill(rt, stage, _forceFull);
  drawCenterTemp(rt, now, _forceFull);
  drawTargetRow(rt, _forceFull);
  drawInfoRow(rt, remainingSec, _forceFull);
  drawHeatIcon(rt, _forceFull);
  drawWifiIcon(rt, _forceFull);
  drawAlarmOverlay(rt, _forceFull);

  _forceFull = false;
  M5Dial.Display.endWrite();
}
