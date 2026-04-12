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

void StageManager::startProfile(uint8_t) {
  if (!_cfg || !_rt) return;
  DBG_LOGF("StageManager: start manual stage temp=%.2f minutes=%lu\n",
           _cfg->localSetpointC,
           static_cast<unsigned long>(_cfg->manualStageMinutes));
  _rt->currentStageIndex = 0;
  _rt->currentSetpointC = _cfg->localSetpointC;
  _rt->activeStageMinutes = _cfg->manualStageMinutes;
  _rt->runState = RunState::Running;
  _rt->uiMode = UiMode::Running;
  _rt->stageTimerStarted = false;
  _rt->stageHoldStartedAtMs = 0;
  _rt->stageStartedAtMs = millis();
  strlcpy(_manualStage.name, "RUNNING", sizeof(_manualStage.name));
  _manualStage.targetC = _rt->currentSetpointC;
  _manualStage.holdSeconds = _rt->activeStageMinutes * 60UL;
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
  return nullptr;
}

const BrewStage* StageManager::getCurrentStage() const {
  if (!_cfg || !_rt) return nullptr;
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
  const uint32_t totalSec = _rt->activeStageMinutes * 60UL;
  if (totalSec == 0) return 0;
  if (!_rt->stageTimerStarted) return totalSec;
  const uint32_t elapsed = (millis() - _rt->stageHoldStartedAtMs) / 1000UL;
  return (elapsed >= totalSec) ? 0 : (totalSec - elapsed);
}

void StageManager::update(float currentTempC) {
  if (!_cfg || !_rt || _rt->runState != RunState::Running) return;

  const uint32_t totalSec = _rt->activeStageMinutes * 60UL;
  if (totalSec == 0) {
    // Timer value of 0 means "hold indefinitely" (no auto-complete).
    _rt->stageTimerStarted = false;
    _rt->stageHoldStartedAtMs = 0;
    return;
  }

  const float targetC = _rt->currentSetpointC;
  if (!_rt->stageTimerStarted && !isnan(currentTempC) && currentTempC >= targetC) {
    _rt->stageTimerStarted = true;
    _rt->stageHoldStartedAtMs = millis();
    DBG_LOGF("StageManager: timer started temp=%.2f target=%.2f\n", currentTempC, targetC);
  }

  if (_rt->stageTimerStarted) {
    const uint32_t elapsedSec = (millis() - _rt->stageHoldStartedAtMs) / 1000UL;
    if (elapsedSec >= totalSec) {
      DBG_LOGLN("StageManager: manual stage complete");
      _rt->runState = RunState::Complete;
      _rt->uiMode = UiMode::SetpointAdjust;
      _rt->pendingProfileCompletePublish = true;
      _rt->stageTimerStarted = false;
      _rt->stageHoldStartedAtMs = 0;
      strlcpy(_manualStage.name, "COMPLETE", sizeof(_manualStage.name));
    }
  }
}
