#include "DisplayManager.h"
#include <cmath>

void DisplayManager::begin() {
  M5Dial.Display.fillScreen(BG);
}

String DisplayManager::formatTime(uint32_t sec) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02lu:%02lu", sec / 60, sec % 60);
  return String(buf);
}

void DisplayManager::drawRing(float progress) {
  const int cx = 120;
  const int cy = 120;
  const int rOuter = 112;
  const int rInner = 98;
  const int start = -225;
  const int sweep = 270;

  M5Dial.Display.fillArc(cx, cy, rOuter, rInner, start, start + sweep, GREY);

  if (progress > 0.0f) {
    int end = start + static_cast<int>(sweep * constrain(progress, 0.0f, 1.0f));
    M5Dial.Display.fillArc(cx, cy, rOuter, rInner, start, end, GOLD);
  }
}

/*void DisplayManager::draw(const PersistentConfig& cfg, const RuntimeState& rt, const BrewStage* stage, uint32_t remainingSec) {
  M5Dial.Display.startWrite();
  M5Dial.Display.fillScreen(BG);

  float progress = 0.0f;
  if (stage && stage->holdSeconds > 0 && rt.stageTimerStarted) {
    progress = static_cast<float>(remainingSec) / static_cast<float>(stage->holdSeconds);
  } else if (stage && stage->holdSeconds > 0) {
    progress = 1.0f;
  }
  drawRing(progress);

  M5Dial.Display.setTextDatum(top_center);
  M5Dial.Display.setTextColor(GOLD, BG);
  M5Dial.Display.setFont(&fonts::Font2);
  M5Dial.Display.drawString("BREW_CORE", 120, 24);

  M5Dial.Display.fillRoundRect(70, 42, 100, 24, 12, 0x18E3);
  M5Dial.Display.fillCircle(82, 54, 4, RED);
  M5Dial.Display.setTextColor(FG, 0x18E3);
  M5Dial.Display.drawString(stage ? stage->name : "IDLE", 124, 47);

  M5Dial.Display.setTextDatum(middle_center);
  M5Dial.Display.setTextColor(FG, BG);
  M5Dial.Display.setFont(&fonts::Font7);
  if (isnan(rt.currentTempC)) {
    M5Dial.Display.drawString("--.-", 120, 108);
  } else {
    char tempBuf[12];
    snprintf(tempBuf, sizeof(tempBuf), "%.1f", rt.currentTempC);
    M5Dial.Display.drawString(tempBuf, 110, 112);
  }
  M5Dial.Display.setTextColor(GOLD, BG);
  M5Dial.Display.setFont(&fonts::Font4);
  M5Dial.Display.drawString("C", 170, 118);

  M5Dial.Display.setTextDatum(top_left);
  M5Dial.Display.setTextColor(GREY, BG);
  M5Dial.Display.setFont(&fonts::Font2);
  M5Dial.Display.drawString("TARGET:", 55, 150);
  M5Dial.Display.drawString("TIME", 55, 168);
  M5Dial.Display.drawString("POWER", 140, 168);

  M5Dial.Display.setTextColor(GOLD, BG);
  M5Dial.Display.setFont(&fonts::Font4);
  char spBuf[12];
  snprintf(spBuf, sizeof(spBuf), "%.1fC", rt.currentSetpointC);
  M5Dial.Display.drawString(spBuf, 118, 148);

  M5Dial.Display.setTextColor(FG, BG);
  M5Dial.Display.drawString(formatTime(remainingSec), 55, 183);

  M5Dial.Display.setTextColor(BLUE, BG);
  char outBuf[12];
  snprintf(outBuf, sizeof(outBuf), "%.0f%%", rt.heaterOutputPct);
  M5Dial.Display.drawString(outBuf, 145, 183);

  M5Dial.Display.setTextColor(GREY, BG);
  M5Dial.Display.setFont(&fonts::Font4);
  M5Dial.Display.drawString("SET", 40, 205);
  M5Dial.Display.drawString(rt.wifiConnected ? "NET" : "OFF", 182, 205);

  M5Dial.Display.fillRoundRect(96, 198, 48, 42, 20, 0x3186);
  M5Dial.Display.fillRect(114, 212, 12, 12, GOLD);

  M5Dial.Display.setTextDatum(top_center);
  M5Dial.Display.setTextColor(rt.activeAlarm == AlarmCode::None ? GOLD : RED, BG);
  M5Dial.Display.setFont(&fonts::Font2);

  String topState;
  topState += (rt.controlMode == ControlMode::Local ? "LOCAL" : "REMOTE");
  topState += " • ";
  switch (rt.runState) {
    case RunState::Idle: topState += "IDLE"; break;
    case RunState::Running: topState += "RUN"; break;
    case RunState::Paused: topState += "PAUSE"; break;
    case RunState::Complete: topState += "DONE"; break;
    case RunState::Fault: topState += "FAULT"; break;
  }
  M5Dial.Display.drawString(topState, 120, 4);

  if (rt.activeAlarm != AlarmCode::None) {
    M5Dial.Display.fillRoundRect(20, 246 - 22, 200, 20, 8, RED);
    M5Dial.Display.setTextColor(TFT_BLACK, RED);
    M5Dial.Display.drawString(rt.alarmText, 120, 226);
  }

  if (rt.editSetpointMode) {
    M5Dial.Display.fillRoundRect(50, 78, 140, 18, 8, 0x18E3);
    M5Dial.Display.setTextColor(GOLD, 0x18E3);
    M5Dial.Display.drawString("EDIT SETPOINT", 120, 80);
  }

  M5Dial.Display.endWrite();
}
*/

void DisplayManager::draw(const PersistentConfig&, const RuntimeState& rt, const BrewStage*, uint32_t) {
  M5Dial.Display.fillScreen(TFT_BLACK);
  M5Dial.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5Dial.Display.setTextDatum(middle_center);
  M5Dial.Display.setFont(&fonts::Font2);

  char buf[32];
  if (isnan(rt.currentTempC)) {
    snprintf(buf, sizeof(buf), "TEMP --.-");
  } else {
    snprintf(buf, sizeof(buf), "TEMP %.1fC", rt.currentTempC);
  }

  M5Dial.Display.drawString(buf, 120, 100);
  M5Dial.Display.drawString("BOOT OK", 120, 130);
}
