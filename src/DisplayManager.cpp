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

const char* menuFooterText(const MenuRenderItem& item, bool editing) {
  if (editing) return "TURN TO ADJUST  PRESS TO CONFIRM";

  switch (item.kind) {
    case MenuItemKind::Submenu: return "PRESS TO OPEN";
    case MenuItemKind::Action: return "PRESS TO RUN";
    case MenuItemKind::Value: return "PRESS TO EDIT";
    case MenuItemKind::Back: return "PRESS TO RETURN";
    case MenuItemKind::Exit: return "PRESS TO EXIT";
    case MenuItemKind::ReadOnly:
    default:
      return "TURN TO SCROLL";
  }
}

void fillInnerRectClipped(lgfx::LGFX_Device& d, int x, int y, int w, int h, uint16_t color) {
  constexpr int kSafeRadius = kRingInner - 2;
  const int radiusSq = kSafeRadius * kSafeRadius;
  const int rectTop = max(0, y);
  const int rectBottom = min(239, y + h - 1);

  for (int py = rectTop; py <= rectBottom; ++py) {
    const int dy = py - kCy;
    const int dySq = dy * dy;
    if (dySq > radiusSq) continue;

    const int maxDx = static_cast<int>(sqrtf(static_cast<float>(radiusSq - dySq)));
    const int clipLeft = max(x, kCx - maxDx);
    const int clipRight = min(x + w - 1, kCx + maxDx);
    if (clipLeft > clipRight) continue;
    d.fillRect(clipLeft, py, clipRight - clipLeft + 1, 1, color);
  }
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
  _lastTestingModeActive = false;
  _lastOperatingMode = static_cast<OperatingMode>(255);
  _lastAlarm = static_cast<AlarmCode>(255);
  _lastRunState = static_cast<RunState>(255);
  _lastUiMode = static_cast<UiMode>(255);

  _lastStageName[0] = '\0';
  _lastTempText[0] = '\0';
  _lastInfoText[0] = '\0';
  _lastMenuTitle[0] = '\0';
  _lastMenuEditing = false;
  _lastMenuItemCount = 0;
  _lastMenuSelectedIndex = 0xFF;
  memset(_lastMenuItems, 0, sizeof(_lastMenuItems));
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

void DisplayManager::drawNoIntegrationOverlay(int x, int y) {
  auto& d = M5Dial.Display;
  const uint16_t overlay = RED;
  d.drawCircle(x, y, 10, overlay);
  d.drawCircle(x, y, 9, overlay);
  d.drawLine(x - 6, y + 6, x + 6, y - 6, overlay);
  d.drawLine(x - 6, y + 5, x + 5, y - 6, overlay);
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

  _staticDrawn = true;
}

bool DisplayManager::menuStateChanged(const MenuRenderState& menuState) const {
  if (_forceFull) return true;
  if (strcmp(_lastMenuTitle, menuState.title) != 0) return true;
  if (_lastMenuEditing != menuState.editing) return true;
  if (_lastMenuItemCount != menuState.itemCount) return true;
  if (_lastMenuSelectedIndex != menuState.selectedIndex) return true;

  for (uint8_t i = 0; i < menuState.itemCount && i < 10; ++i) {
    const MenuRenderItem& prev = _lastMenuItems[i];
    const MenuRenderItem& next = menuState.items[i];
    if (prev.id != next.id || prev.kind != next.kind || prev.selected != next.selected) return true;
    if (strcmp(prev.label, next.label) != 0) return true;
    if (strcmp(prev.value, next.value) != 0) return true;
  }

  return false;
}

void DisplayManager::cacheMenuState(const MenuRenderState& menuState) {
  strlcpy(_lastMenuTitle, menuState.title, sizeof(_lastMenuTitle));
  _lastMenuEditing = menuState.editing;
  _lastMenuItemCount = menuState.itemCount;
  _lastMenuSelectedIndex = menuState.selectedIndex;
  memset(_lastMenuItems, 0, sizeof(_lastMenuItems));
  for (uint8_t i = 0; i < menuState.itemCount && i < 10; ++i) {
    _lastMenuItems[i] = menuState.items[i];
  }
}

void DisplayManager::drawMenuScreen(const MenuRenderState& menuState) {
  static M5Canvas menuCanvas(&M5Dial.Display);
  static bool menuCanvasReady = false;
  if (!menuCanvasReady) {
    menuCanvas.setColorDepth(16);
    menuCanvas.createSprite(240, 240);
    menuCanvasReady = true;
  }

  auto& d = menuCanvas;
  const uint16_t menuBg = rgb(7, 11, 16);
  const uint16_t menuOuter = rgb(20, 34, 42);
  const uint16_t menuInner = rgb(14, 24, 31);
  const uint16_t menuAccent = rgb(38, 68, 82);
  const int centerY = 120;
  const int selectedBoxY = 92;
  d.fillScreen(BG);
  d.fillCircle(kCx, kCy, 116, menuBg);
  d.drawCircle(kCx, kCy, 116, menuOuter);
  d.drawCircle(kCx, kCy, 108, menuInner);
  d.drawArc(kCx, kCy, 116, 114, 210, 320, menuAccent);

  d.setTextDatum(top_center);
  d.setTextColor(FG_MUTED, BG);
  d.setFont(&fonts::Font2);
  d.drawString(menuState.title[0] ? menuState.title : "MENU", 120, 18);

  const int selected = menuState.selectedIndex;
  d.drawRoundRect(22, selectedBoxY, 196, 54, 16, menuState.editing ? GOLD : rgb(58, 98, 116));

  for (int distance = -2; distance <= 2; ++distance) {
    const int index = selected + distance;
    if (index < 0 || index >= menuState.itemCount) continue;

    const MenuRenderItem& item = menuState.items[index];
    const bool isSelected = (distance == 0);
    const int absDistance = abs(distance);

    int y = centerY;
    const lgfx::IFont* font = &fonts::Font4;
    uint16_t color = FG;

    if (isSelected) {
      y = (item.value[0] != '\0') ? centerY - 8 : centerY;
      font = &fonts::Font4;
      color = FG;
    } else if (absDistance == 1) {
      y = centerY + (distance * 42) - 7;
      font = &fonts::Font2;
      color = rgb(182, 194, 202);
    } else {
      y = centerY + (distance * 70) - 5;
      font = &fonts::Font0;
      color = rgb(110, 124, 132);
    }

    d.setFont(font);
    d.setTextDatum(middle_center);
    d.setTextColor(color, menuBg);
    d.drawString(item.label, 120, y);

    if (!isSelected) continue;

    if (item.kind == MenuItemKind::Submenu) {
      d.setTextDatum(middle_right);
      d.setFont(&fonts::Font2);
      d.setTextColor(rgb(144, 198, 224), menuBg);
      d.drawString(">", 206, centerY);
    }

    if (item.value[0] != '\0') {
      d.setTextDatum(top_center);
      d.setFont(&fonts::Font2);
      d.setTextColor(menuState.editing ? GOLD : rgb(132, 205, 238), menuBg);
      d.drawString(item.value, 120, centerY + 8);
    }
  }

  if (menuState.itemCount > 0) {
    const MenuRenderItem& selectedItem = menuState.items[menuState.selectedIndex];
    d.setTextDatum(bottom_center);
    d.setFont(&fonts::Font0);
    d.setTextColor(rgb(120, 130, 136), menuBg);
    d.drawString(menuFooterText(selectedItem, menuState.editing), 120, 226);
  }

  menuCanvas.pushSprite(0, 0);
}

void DisplayManager::drawMessageScreen(const char* title, const char* detail, const char* footer) {
  auto& d = M5Dial.Display;
  d.fillScreen(BG);

  d.fillCircle(kCx, kCy, 116, rgb(7, 11, 16));
  d.drawCircle(kCx, kCy, 116, rgb(20, 34, 42));
  d.drawCircle(kCx, kCy, 108, rgb(14, 24, 31));
  d.drawArc(kCx, kCy, 116, 114, 210, 320, rgb(38, 68, 82));

  d.setTextDatum(middle_center);
  d.setTextColor(FG, BG);
  d.setFont(&fonts::Font4);
  d.drawString(title ? title : "", 120, 88);

  d.setFont(&fonts::Font2);
  d.setTextColor(rgb(182, 194, 202), BG);
  d.drawString(detail ? detail : "", 120, 126);

  d.setFont(&fonts::Font0);
  d.setTextColor(rgb(132, 205, 238), BG);
  d.drawString(footer ? footer : "", 120, 174);
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
  else if (rt.uiMode == UiMode::AutoTuneActive || rt.runState == RunState::AutoTune) text = "AUTOTUNE";
  else if (rt.uiMode == UiMode::SetpointAdjust) text = "SET TEMP";
  else if (rt.uiMode == UiMode::StageTimeAdjust) text = "SET TIME";
  else if (rt.runState == RunState::Complete) text = "COMPLETE";
  else if (rt.runState == RunState::Paused) text = "PAUSED";
  else if (rt.runState == RunState::Fault) text = "FAULT";
  else if (stage) text = stage->name;
  else text = "IDLE";

  if (!force &&
      strcmp(_lastStageName, text) == 0 &&
      _lastAlarm == rt.activeAlarm &&
      _lastRunState == rt.runState &&
      _lastUiMode == rt.uiMode &&
      _lastControlAuthority == rt.controlAuthority) return;

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
    if (rt.controlAuthority == ControlAuthority::LocalOverride) {
      d.drawString(text, kPillCenterX, kPillY + 1);
      d.setFont(&fonts::Font0);
      d.setTextColor(GOLD, BG);
      d.drawString("LOCAL OVR", kPillCenterX, kPillY + 14);
    } else {
      d.drawString(text, kPillCenterX, kPillY + 5);
    }
  }

  strlcpy(_lastStageName, text, sizeof(_lastStageName));
  _lastAlarm = rt.activeAlarm;
  _lastRunState = rt.runState;
  _lastUiMode = rt.uiMode;
  _lastControlAuthority = rt.controlAuthority;
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
    fillInnerRectClipped(d, kTempBoxX, kTempBoxY, kTempBoxW, kTempBoxH, BG);

    d.setTextDatum(middle_center);
    d.setTextColor(rgb(90, 70, 28), BG);
    d.drawString(tempBuf, 116, 104);
    d.setTextColor(FG, BG);
    d.drawString(tempBuf, 114, 102);

    d.setFont(&fonts::Font2);
    d.setTextColor(GOLD, BG);
    d.setTextDatum(top_left);
    d.drawString("C", 176, 82);
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
  if (rt.uiMode == UiMode::AutoTuneActive || rt.runState == RunState::AutoTune) {
    infoBuf[0] = '\0';
  } else if (rt.uiMode == UiMode::StageTimeAdjust) {
    snprintf(infoBuf, sizeof(infoBuf), "%s", formatMinutes(rt.activeStageMinutes).c_str());
  } else if (rt.activeStageMinutes == 0 && (rt.runState == RunState::Running || rt.runState == RunState::Paused)) {
    snprintf(infoBuf, sizeof(infoBuf), "INF");
  } else {
    snprintf(infoBuf, sizeof(infoBuf), "%s", formatTime(remainingSec).c_str());
  }

  if (!force && strcmp(_lastInfoText, infoBuf) == 0 && _lastUiMode == rt.uiMode) return;

  auto& d = M5Dial.Display;
  fillInnerRectClipped(d, kInfoRowX, kInfoRowY, kInfoRowW, kInfoRowH, BG);
  d.setTextDatum(top_center);
  d.setFont(&fonts::Font4);
  d.setTextColor(FG, BG);
  d.drawString(infoBuf, 120, 176);

  strlcpy(_lastInfoText, infoBuf, sizeof(_lastInfoText));
  _lastRemainingSec = remainingSec;
}

void DisplayManager::drawTestingBadge(const RuntimeState& rt, bool force) {
  if (!force && _lastTestingModeActive == rt.testingModeActive) return;

  auto& d = M5Dial.Display;
  fillInnerRectClipped(d, 28, 116, 28, 24, BG);

  if (rt.testingModeActive) {
    d.setTextDatum(top_center);
    d.setFont(&fonts::Font2);
    d.setTextColor(RED, BG);
    d.drawString("T", 42, 118);
  }

  _lastTestingModeActive = rt.testingModeActive;
}

void DisplayManager::drawHeatIcon(const RuntimeState& rt, bool force) {
  const bool on = rt.heatOn;
  if (!force && _lastHeatOn == on) return;

  auto& d = M5Dial.Display;
  fillInnerRectClipped(d, 184, 84, 28, 36, BG);
  drawFireGlyph(198, kIconCenterY, on ? RED : OUTLINE_SOFT);

  _lastHeatOn = on;
}

void DisplayManager::drawWifiIcon(const RuntimeState& rt, bool force) {
  if (!force &&
      _lastWifiConnected == rt.wifiConnected &&
      _lastMqttConnected == rt.mqttConnected &&
      _lastOperatingMode == rt.operatingMode) return;

  auto& d = M5Dial.Display;
  fillInnerRectClipped(d, 28, 84, 28, 28, BG);

  uint16_t color = OUTLINE_SOFT;
  if (rt.operatingMode == OperatingMode::Standalone) {
    if (rt.wifiConnected && rt.mqttConnected) color = BLUE;
    else if (rt.wifiConnected) color = GOLD;
    else color = rgb(92, 132, 116);
    drawWifiGlyph(42, kIconCenterY, color);
    drawNoIntegrationOverlay(42, kIconCenterY - 4);
  } else {
    if (rt.wifiConnected && rt.mqttConnected) color = BLUE;
    else if (rt.wifiConnected) color = GOLD;
    drawWifiGlyph(42, kIconCenterY, color);
  }

  _lastWifiConnected = rt.wifiConnected;
  _lastMqttConnected = rt.mqttConnected;
  _lastOperatingMode = rt.operatingMode;
}

void DisplayManager::draw(const PersistentConfig&, const RuntimeState& rt, const ProcessStage* stage, uint32_t remainingSec, const MenuRenderState* menuState) {
  const uint32_t now = millis();
  if (rt.uiMode == UiMode::AutoTuneIntro) {
    if (_forceFull || _lastUiMode != rt.uiMode) {
      M5Dial.Display.startWrite();
      drawMessageScreen("Autotune beginning", "To exit, hold the button", "for 5 seconds");
      M5Dial.Display.endWrite();
      _lastUiMode = rt.uiMode;
      _forceFull = false;
    }
    return;
  }

  if (rt.uiMode == UiMode::AutoTuneComplete) {
    if (_forceFull || _lastUiMode != rt.uiMode) {
      M5Dial.Display.startWrite();
      drawMessageScreen("Autotune Complete", "Tuned values are ready", "Press button to return");
      M5Dial.Display.endWrite();
      _lastUiMode = rt.uiMode;
      _forceFull = false;
    }
    return;
  }

  if (rt.uiMode == UiMode::SettingsAdjust && menuState) {
    if (!menuStateChanged(*menuState)) return;
    M5Dial.Display.startWrite();
    drawMenuScreen(*menuState);
    M5Dial.Display.endWrite();
    cacheMenuState(*menuState);
    _forceFull = false;
    return;
  }

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
  drawTestingBadge(rt, _forceFull);
  drawHeatIcon(rt, _forceFull);
  drawWifiIcon(rt, _forceFull);
  M5Dial.Display.endWrite();

  _forceFull = false;
}
