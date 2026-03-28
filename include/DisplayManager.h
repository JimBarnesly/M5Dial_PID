#pragma once
#include <M5Dial.h>
#include "AppState.h"

class DisplayManager {
public:
  void begin();
  void draw(const PersistentConfig& cfg, const RuntimeState& rt, const BrewStage* stage, uint32_t remainingSec);

private:
  void drawRing(float progress);
  String formatTime(uint32_t sec);
  const uint16_t BG = 0x0000;
  const uint16_t FG = 0xFFDE;
  const uint16_t GOLD = 0xF5C0;
  const uint16_t BLUE = 0x25BF;
  const uint16_t RED = 0xE2E8;
  const uint16_t GREY = 0x4208;
};
