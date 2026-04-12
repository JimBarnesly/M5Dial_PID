#include "StageManager.h"
#include "DebugControl.h"
#include <Arduino.h>
#include <cstring>

void StageManager::begin(PersistentConfig* cfg, RuntimeState* rt) {
  _cfg = cfg;
  _rt = rt;
  memset(&_manualStage, 0, sizeof(_manualStage));
  strlcpy(_manualStage.name, "MANUAL STAGE", sizeof(_manualStage.name));
}

void StageManager::startProfile(uint8_t index) {
  if (!_cfg || !_rt) return;

  if (_cfg->profileCount == 0 || index >= _cfg->profileCount || index >= Config::MAX_PROFILES) {
    DBG_LOGF("StageManager: invalid profile index=%u profileCount=%u\n",
             static_cast<unsigned>(index),
             static_cast<unsigned>(_cfg->profileCount));
    return;
  }

  BrewProfile& profile = _cfg->profiles[index];
  if (profile.stageCount == 0 || profile.stageCount > Config::MAX_STAGES) {
    DBG_LOGF("StageManager: profile '%s' has invalid stageCount=%u\n",
             profile.name,
             static_cast<unsigned>(profile.stageCount));
    return;
  }

  _cfg->activeProfileIndex = index;
  const BrewStage& stage = profile.stages[0];
  DBG_LOGF("StageManager: start profile index=%u name='%s' stageCount=%u firstTarget=%.2f firstHold=%lu\n",
           static_cast<unsigned>(index),
           profile.name,
           static_cast<unsigned>(profile.stageCount),
           stage.targetC,
           static_cast<unsigned long>(stage.holdSeconds));

  _rt->currentStageIndex = 0;
  _rt->currentSetpointC = stage.targetC;
  _rt->activeStageMinutes = (stage.holdSeconds + 59UL) / 60UL;
  _rt->runState = RunState::Running;
  _rt->uiMode = UiMode::Running;
  _rt->stageTimerStarted = false;
  _rt->stageHoldStartedAtMs = 0;
  _rt->stageStartedAtMs = millis();
}

void StageManager::pause() {
  if (_rt && _rt->runState == RunState::Running) {
    DBG_LOGLN("StageManager: pause");
    _rt->runState = RunState::Paused;
    _rt->uiMode = UiMode::Paused;
    strlcpy(_manualStage.name, "PAUSED", sizeof(_manualStage.name));
  }
}

void StageManager::resume() {
  if (_rt && _rt->runState == RunState::Paused) {
    DBG_LOGLN("StageManager: resume");
    _rt->runState = RunState::Running;
    _rt->uiMode = UiMode::Running;
    strlcpy(_manualStage.name, "RUNNING", sizeof(_manualStage.name));
  }
}

void StageManager::stop() {
  if (!_rt) return;
  DBG_LOGLN("StageManager: stop");
  _rt->runState = RunState::Idle;
  _rt->uiMode = UiMode::SetpointAdjust;
  _rt->currentStageIndex = 0;
  _rt->stageTimerStarted = false;
  _rt->stageHoldStartedAtMs = 0;
  _rt->activeStageMinutes = _cfg ? _cfg->manualStageMinutes : 0;
  strlcpy(_manualStage.name, "IDLE", sizeof(_manualStage.name));
  _manualStage.targetC = _cfg ? _cfg->localSetpointC : 0.0f;
  _manualStage.holdSeconds = (_cfg ? _cfg->manualStageMinutes : 0) * 60UL;
}

const BrewProfile* StageManager::getActiveProfile() const {
  if (!_cfg) return nullptr;
  if (_cfg->profileCount == 0 || _cfg->activeProfileIndex >= _cfg->profileCount || _cfg->activeProfileIndex >= Config::MAX_PROFILES) {
    return nullptr;
  }
  const BrewProfile& profile = _cfg->profiles[_cfg->activeProfileIndex];
  if (profile.stageCount == 0 || profile.stageCount > Config::MAX_STAGES) return nullptr;
  return &profile;
}

const BrewStage* StageManager::getCurrentStage() const {
  if (!_cfg || !_rt) return nullptr;
  const BrewProfile* profile = getActiveProfile();
  if (profile && _rt->currentStageIndex < profile->stageCount) {
    return &profile->stages[_rt->currentStageIndex];
  }

  switch (_rt->uiMode) {
    case UiMode::SetpointAdjust:
      strlcpy(_manualStage.name, "SET TEMP", sizeof(_manualStage.name));
      break;
    case UiMode::StageTimeAdjust:
      strlcpy(_manualStage.name, "SET TIME", sizeof(_manualStage.name));
      break;
    case UiMode::Running:
      strlcpy(_manualStage.name, "RUNNING", sizeof(_manualStage.name));
      break;
    case UiMode::Paused:
      strlcpy(_manualStage.name, "PAUSED", sizeof(_manualStage.name));
      break;
    case UiMode::SettingsAdjust:
      strlcpy(_manualStage.name, "SETTINGS", sizeof(_manualStage.name));
      break;
  }
  _manualStage.targetC = _rt->currentSetpointC;
  _manualStage.holdSeconds = _rt->activeStageMinutes * 60UL;
  return &_manualStage;
}

uint32_t StageManager::getRemainingSeconds() const {
  if (!_rt) return 0;
  uint32_t totalSec = _rt->activeStageMinutes * 60UL;
  const BrewStage* stage = getCurrentStage();
  if (stage) totalSec = stage->holdSeconds;
  if (totalSec == 0) return 0;
  if (!_rt->stageTimerStarted) return totalSec;
  const uint32_t elapsed = (millis() - _rt->stageHoldStartedAtMs) / 1000UL;
  return (elapsed >= totalSec) ? 0 : (totalSec - elapsed);
}

void StageManager::update(float currentTempC) {
  if (!_cfg || !_rt || _rt->runState != RunState::Running) return;

  const BrewProfile* profile = getActiveProfile();
  if (!profile || _rt->currentStageIndex >= profile->stageCount) return;

  const BrewStage& stage = profile->stages[_rt->currentStageIndex];
  _rt->currentSetpointC = stage.targetC;
  _rt->activeStageMinutes = (stage.holdSeconds + 59UL) / 60UL;
  const uint32_t totalSec = stage.holdSeconds;
  if (totalSec == 0) {
    // Timer value of 0 means "hold indefinitely" (no auto-complete).
    _rt->stageTimerStarted = false;
    _rt->stageHoldStartedAtMs = 0;
    return;
  }

  const float targetC = stage.targetC;
  if (!_rt->stageTimerStarted && !isnan(currentTempC) && currentTempC >= targetC) {
    _rt->stageTimerStarted = true;
    _rt->stageHoldStartedAtMs = millis();
    DBG_LOGF("StageManager: timer started temp=%.2f target=%.2f\n", currentTempC, targetC);
  }

  if (_rt->stageTimerStarted) {
    const uint32_t elapsedSec = (millis() - _rt->stageHoldStartedAtMs) / 1000UL;
    if (elapsedSec >= totalSec) {
      _rt->stageTimerStarted = false;
      _rt->stageHoldStartedAtMs = 0;

      const uint8_t nextIndex = _rt->currentStageIndex + 1;
      if (nextIndex < profile->stageCount) {
        _rt->currentStageIndex = nextIndex;
        const BrewStage& nextStage = profile->stages[nextIndex];
        _rt->currentSetpointC = nextStage.targetC;
        _rt->activeStageMinutes = (nextStage.holdSeconds + 59UL) / 60UL;
        _rt->stageStartedAtMs = millis();
        DBG_LOGF("StageManager: stage complete, advancing to index=%u target=%.2f hold=%lu\n",
                 static_cast<unsigned>(nextIndex),
                 nextStage.targetC,
                 static_cast<unsigned long>(nextStage.holdSeconds));
      } else {
        DBG_LOGF("StageManager: profile complete index=%u name='%s'\n",
                 static_cast<unsigned>(_cfg->activeProfileIndex),
                 profile->name);
        _rt->runState = RunState::Complete;
        _rt->uiMode = UiMode::SetpointAdjust;
        _rt->pendingProfileCompletePublish = true;
      }
    }
  }
}
