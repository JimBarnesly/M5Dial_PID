#include "StageManager.h"
#include "Config.h"
#include <Arduino.h>
#include <cmath>

void StageManager::begin(PersistentConfig* cfg, RuntimeState* rt) {
  _cfg = cfg;
  _rt = rt;
}

void StageManager::startProfile(uint8_t index) {
  if (!_cfg || !_rt || index >= _cfg->profileCount) return;
  _cfg->activeProfileIndex = index;
  _rt->currentStageIndex = 0;
  _rt->runState = RunState::Running;
  _rt->stageTimerStarted = false;
  _rt->stageHoldStartedAtMs = 0;
  _rt->stageStartedAtMs = millis();

  const BrewStage& s = _cfg->profiles[index].stages[0];
  _rt->currentSetpointC = s.targetC;
}

void StageManager::pause() {
  if (_rt && _rt->runState == RunState::Running) _rt->runState = RunState::Paused;
}

void StageManager::resume() {
  if (_rt && _rt->runState == RunState::Paused) _rt->runState = RunState::Running;
}

void StageManager::stop() {
  if (!_rt) return;
  _rt->runState = RunState::Idle;
  _rt->currentStageIndex = 0;
  _rt->stageTimerStarted = false;
  _rt->stageHoldStartedAtMs = 0;
}

const BrewProfile* StageManager::getActiveProfile() const {
  if (!_cfg || _cfg->profileCount == 0) return nullptr;
  return &_cfg->profiles[_cfg->activeProfileIndex];
}

const BrewStage* StageManager::getCurrentStage() const {
  const BrewProfile* profile = getActiveProfile();
  if (!profile || !_rt || _rt->currentStageIndex >= profile->stageCount) return nullptr;
  return &profile->stages[_rt->currentStageIndex];
}

uint32_t StageManager::getRemainingSeconds() const {
  const BrewStage* stage = getCurrentStage();
  if (!stage || !_rt || !_rt->stageTimerStarted) return stage ? stage->holdSeconds : 0;

  const uint32_t elapsed = (millis() - _rt->stageHoldStartedAtMs) / 1000;
  return (elapsed >= stage->holdSeconds) ? 0 : (stage->holdSeconds - elapsed);
}

void StageManager::update(float currentTempC) {
  if (!_cfg || !_rt || _rt->runState != RunState::Running) return;

  const BrewProfile* profile = getActiveProfile();
  if (!profile) return;
  if (_rt->currentStageIndex >= profile->stageCount) {
    _rt->runState = RunState::Complete;
    return;
  }

  const BrewStage& stage = profile->stages[_rt->currentStageIndex];
  _rt->currentSetpointC = stage.targetC;

  const float threshold = stage.targetC - _cfg->stageStartBandC;
  const bool nearTarget = fabs(currentTempC - stage.targetC) <= Config::STAGE_AT_TEMP_BAND_C;

  if (!_rt->stageTimerStarted && currentTempC >= threshold) {
    _rt->stageTimerStarted = true;
    _rt->stageHoldStartedAtMs = millis();
  }

  if (_rt->stageTimerStarted) {
    const uint32_t elapsedSec = (millis() - _rt->stageHoldStartedAtMs) / 1000;
    if (elapsedSec >= stage.holdSeconds) {
      _rt->currentStageIndex++;
      _rt->stageTimerStarted = false;
      _rt->stageHoldStartedAtMs = 0;
      _rt->stageStartedAtMs = millis();

      if (_rt->currentStageIndex >= profile->stageCount) {
        _rt->runState = RunState::Complete;
        _rt->pendingProfileCompletePublish = true;
      }
    }
  }

  (void)nearTarget;
}
