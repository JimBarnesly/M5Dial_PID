#include "DisplayManager.h"
#include "Config.h"
#include <cmath>
#include <cstring>

namespace {
constexpr int kCx = 120;
constexpr int kCy = 120;
}

void DisplayManager::begin() {
  drawStaticUi();
  invalidateAll();
}

void DisplayManager::invalidateAll() {
  _forceFull = true;
  _ringDirty = true;
  _lastTempDrawMs = 0;
  _lastStatusDrawMs = 0;
  _lastRingDrawMs = 0;
  _lastForceRefreshMs = 0;
  _lastTempC = NAN;
  _lastSetpointC = NAN;
  _lastHeaterPct = -999.0f;
  _lastRemainingSec = UINT32_MAX;
  _lastWifiConnected = !_lastWifiConnected;
  _lastMqttConnected = !_lastMqttConnected;
  _lastEditMode = !_lastEditMode;
  _lastAlarm = static_cast<AlarmCode>(255);
  _lastTopState[0] = '\0';
  _lastStageName[0] = '\0';
  _lastAlarmText[0] = '\0';
}

String DisplayManager::formatTime(uint32_t sec) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02lu:%02lu", sec / 60, sec % 60);
  return String(buf);
}

void DisplayManager::drawStaticUi() {
  auto& d = M5Dial.Display;
  d.startWrite();
  d.fillScreen(BG);
  d.setTextDatum(top_center);
  d.setTextColor(GOLD, BG);
  d.setFont(&fonts::Font2);
  d.drawString("BREW_CORE", 120, 24);

  d.drawCircle(kCx, kCy, 112, GREY);
  d.drawCircle(kCx, kCy, 111, GREY);
  d.drawCircle(kCx, kCy, 98, GREY);
  d.drawCircle(kCx, kCy, 97, GREY);

  d.fillRoundRect(96, 198, 48, 42, 20, BTN);
  d.fillRect(114, 212, 12, 12, GOLD);

  d.setTextDatum(top_left);
  d.setTextColor(GREY, BG);
  d.setFont(&fonts::Font2);
  d.drawString("SET", 22, 205);
  d.drawString("NET", 176, 205);
  d.drawString("TARGET:", 55, 150);
  d.drawString("TIME", 55, 168);
  d.drawString("POWER", 140, 168);

  _staticDrawn = true;
  d.endWrite();
}

void DisplayManager::drawRing(float progress) {
  auto& d = M5Dial.Display;
  const int rOuter = 112;
  const int rInner = 98;
  const int start = -225;
  const int sweep = 270;

  d.fillArc(kCx, kCy, rOuter, rInner, start, start + sweep, GREY);
  if (progress > 0.0f) {
    const int end = start + static_cast<int>(sweep * constrain(progress, 0.0f, 1.0f));
    d.fillArc(kCx, kCy, rOuter, rInner, start, end, GOLD);
  }
}

void DisplayManager::drawCenterTemp(const RuntimeState& rt, uint32_t now) {
  if (!_forceFull && now - _lastTempDrawMs < Config::UI_TEMP_TEXT_MS) return;
  if (!_forceFull && !isnan(rt.currentTempC) && !isnan(_lastTempC) && fabsf(rt.currentTempC - _lastTempC) < 0.05f) return;
  if (!_forceFull && isnan(rt.currentTempC) == isnan(_lastTempC) && isnan(rt.currentTempC)) return;

  auto& d = M5Dial.Display;
  d.fillRect(40, 72, 150, 64, BG);
  d.fillRect(166, 100, 30, 30, BG);
  d.setTextDatum(middle_center);
  d.setTextColor(FG, BG);
  d.setFont(&fonts::Font7);

  if (isnan(rt.currentTempC)) {
    d.drawString("--.-", 110, 112);
  } else {
    char tempBuf[12];
    snprintf(tempBuf, sizeof(tempBuf), "%.1f", rt.currentTempC);
    d.drawString(tempBuf, 110, 112);
  }

  d.setTextColor(GOLD, BG);
  d.setFont(&fonts::Font4);
  d.drawString("C", 178, 118);

  _lastTempC = rt.currentTempC;
  _lastTempDrawMs = now;
}

void DisplayManager::drawSetpoint(const RuntimeState& rt, bool force) {
  if (!force && !std::isnan(_lastSetpointC) && fabsf(rt.currentSetpointC - _lastSetpointC) < 0.001f && _lastEditMode == rt.editSetpointMode) {
    return;
  }

  auto& d = M5Dial.Display;
  d.fillRect(92, 146, 74, 28, BG);
  d.setTextDatum(top_left);
  d.setTextColor(GOLD, BG);
  d.setFont(&fonts::Font4);
  char spBuf[16];
  snprintf(spBuf, sizeof(spBuf), "%.1fC", rt.currentSetpointC);
  d.drawString(spBuf, 98, 148);
  _lastSetpointC = rt.currentSetpointC;
}

void DisplayManager::drawTimeAndPower(const RuntimeState& rt, uint32_t remainingSec, bool force) {
  if (!force && _lastRemainingSec == remainingSec && fabsf(_lastHeaterPct - rt.heaterOutputPct) < 0.5f) {
    return;
  }

  auto& d = M5Dial.Display;
  d.fillRect(48, 182, 76, 22, BG);
  d.fillRect(138, 182, 56, 22, BG);

  d.setTextDatum(top_left);
  d.setTextColor(FG, BG);
  d.setFont(&fonts::Font4);
  d.drawString(formatTime(remainingSec), 55, 183);

  d.setTextColor(BLUE, BG);
  char outBuf[12];
  snprintf(outBuf, sizeof(outBuf), "%.0f%%", rt.heaterOutputPct);
  d.drawString(outBuf, 145, 183);

  _lastRemainingSec = remainingSec;
  _lastHeaterPct = rt.heaterOutputPct;
}

void DisplayManager::drawTopState(const RuntimeState& rt, bool force) {
  char topState[40];
  snprintf(topState, sizeof(topState), "%s | %s",
           rt.controlMode == ControlMode::Local ? "LOCAL" : "REMOTE",
           rt.runState == RunState::Idle ? "IDLE" :
           rt.runState == RunState::Running ? "RUN" :
           rt.runState == RunState::Paused ? "PAUSE" :
           rt.runState == RunState::Complete ? "DONE" : "FAULT");

  if (!force && strcmp(_lastTopState, topState) == 0 && _lastAlarm == rt.activeAlarm) return;

  auto& d = M5Dial.Display;
  d.fillRect(8, 2, 224, 18, BG);
  d.setTextDatum(top_center);
  d.setTextColor(rt.activeAlarm == AlarmCode::None ? GOLD : RED, BG);
  d.setFont(&fonts::Font2);
  d.drawString(topState, 120, 4);
  strlcpy(_lastTopState, topState, sizeof(_lastTopState));
}

void DisplayManager::drawStageName(const BrewStage* stage, bool force) {
  const char* name = stage ? stage->name : "IDLE";
  if (!force && strcmp(_lastStageName, name) == 0) return;

  auto& d = M5Dial.Display;
  d.fillRoundRect(70, 42, 100, 24, 12, MID);
  d.fillCircle(82, 54, 4, RED);
  d.setTextDatum(top_center);
  d.setTextColor(FG, MID);
  d.setFont(&fonts::Font2);
  d.drawString(name, 124, 47);
  strlcpy(_lastStageName, name, sizeof(_lastStageName));
}

void DisplayManager::drawAlarm(const RuntimeState& rt, bool force) {
  if (!force && _lastAlarm == rt.activeAlarm && strcmp(_lastAlarmText, rt.alarmText) == 0) return;

  auto& d = M5Dial.Display;
  d.fillRect(20, 222, 200, 22, BG);

  if (rt.activeAlarm != AlarmCode::None) {
    d.fillRoundRect(20, 224, 200, 18, 8, RED);
    d.setTextDatum(top_center);
    d.setTextColor(TFT_BLACK, RED);
    d.setFont(&fonts::Font2);
    d.drawString(rt.alarmText, 120, 226);
  }

  _lastAlarm = rt.activeAlarm;
  strlcpy(_lastAlarmText, rt.alarmText, sizeof(_lastAlarmText));
}

void DisplayManager::drawEditBadge(const RuntimeState& rt, bool force) {
  if (!force && _lastEditMode == rt.editSetpointMode) return;

  auto& d = M5Dial.Display;
  d.fillRect(50, 78, 140, 20, BG);
  if (rt.editSetpointMode) {
    d.fillRoundRect(50, 78, 140, 18, 8, MID);
    d.setTextDatum(top_center);
    d.setTextColor(GOLD, MID);
    d.setFont(&fonts::Font2);
    d.drawString("EDIT SETPOINT", 120, 80);
  }
  _lastEditMode = rt.editSetpointMode;
}

void DisplayManager::drawNetStatus(const RuntimeState& rt, bool force) {
  if (!force && _lastWifiConnected == rt.wifiConnected && _lastMqttConnected == rt.mqttConnected) return;

  auto& d = M5Dial.Display;
  d.fillRect(170, 202, 48, 20, BG);
  d.setTextDatum(top_left);
  d.setFont(&fonts::Font2);

  const char* label = "OFF";
  uint16_t color = GREY;
  if (rt.wifiConnected && rt.mqttConnected) {
    label = "MQTT";
    color = GREEN;
  } else if (rt.wifiConnected) {
    label = "WIFI";
    color = GOLD;
  }

  d.setTextColor(color, BG);
  d.drawString(label, 176, 205);

  _lastWifiConnected = rt.wifiConnected;
  _lastMqttConnected = rt.mqttConnected;
}

void DisplayManager::draw(const PersistentConfig&, const RuntimeState& rt, const BrewStage* stage, uint32_t remainingSec) {
  const uint32_t now = millis();
  if (!_staticDrawn) drawStaticUi();
  if (!_forceFull && now - _lastUiServiceMs < Config::UI_SERVICE_MS) return;
  _lastUiServiceMs = now;

  if (_forceFull || now - _lastForceRefreshMs >= Config::UI_FORCE_REFRESH_MS) {
    drawStaticUi();
    _forceFull = true;
    _ringDirty = true;
    _lastForceRefreshMs = now;
  }

  M5Dial.Display.startWrite();

  const float progress = (stage && stage->holdSeconds > 0)
                           ? (rt.stageTimerStarted
                                ? static_cast<float>(remainingSec) / static_cast<float>(stage->holdSeconds)
                                : 1.0f)
                           : 0.0f;

  if (_forceFull || _ringDirty || now - _lastRingDrawMs >= Config::UI_RING_MS) {
    drawRing(progress);
    _lastRingDrawMs = now;
    _ringDirty = false;
  }

  drawTopState(rt, _forceFull || now - _lastStatusDrawMs >= Config::UI_STATUS_TEXT_MS);
  drawStageName(stage, _forceFull);
  drawCenterTemp(rt, now);
  drawSetpoint(rt, _forceFull);
  drawTimeAndPower(rt, remainingSec, _forceFull);
  drawEditBadge(rt, _forceFull);
  drawAlarm(rt, _forceFull);
  drawNetStatus(rt, _forceFull || now - _lastStatusDrawMs >= Config::UI_STATUS_TEXT_MS);

  _lastStatusDrawMs = now;
  _forceFull = false;

  M5Dial.Display.endWrite();
}
