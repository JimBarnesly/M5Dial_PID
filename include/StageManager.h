#pragma once
#include "AppState.h"

class StageManager {
public:
  void begin(PersistentConfig* cfg, RuntimeState* rt);
  void startProfile(uint8_t index = 0);
  void pause();
  void resume();
  void stop();
  void update(float currentTempC);
  const BrewProfile* getActiveProfile() const;
  const BrewStage* getCurrentStage() const;
  uint32_t getRemainingSeconds() const;

private:
  PersistentConfig* _cfg {nullptr};
  RuntimeState* _rt {nullptr};
  mutable BrewStage _manualStage {};
};
