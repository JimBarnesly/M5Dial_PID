#include <Arduino.h>
#include <M5Dial.h>
#include <ArduinoJson.h>
#include <cmath>
#include <cstring>

#include "Config.h"
#include "AppState.h"
#include "TempSensor.h"
#include "PidController.h"
#include "HeaterOutput.h"
#include "AlarmManager.h"
#include "StorageManager.h"
#include "StageManager.h"
#include "WifiManagerWrapper.h"
#include "MqttManager.h"
#include "DisplayManager.h"
#include "DebugControl.h"

bool gDebugEnabled = true;
bool gDebugDisableWifi = true;
bool gDebugDisableMqtt = true;
bool gDebugVerboseInput = false;

#define DBG_BEGIN(...) do { if (gDebugEnabled) Serial.begin(__VA_ARGS__); } while (0)
#define DBG_PRINTLN(x) DBG_LOGLN(x)
#define DBG_PRINT(x) DBG_LOG(x)
#define DBG_PRINTF(...) DBG_LOGF(__VA_ARGS__)

PersistentConfig gCfg;
RuntimeState gRt;
TempSensor gTempSensor(Config::PIN_ONEWIRE);
PidController gPid;
HeaterOutput gHeater(Config::PIN_HEATER, Config::HEATER_ACTIVE_HIGH);
AlarmManager gAlarm(Config::PIN_BUZZER);
StorageManager gStorage;
StageManager gStages;
WifiManagerWrapper gWifi;
MqttManager gMqtt;
DisplayManager gDisplay;
uint32_t gLastStatusMs = 0, gLastPidMs = 0, gLastMqttServiceMs = 0, gHeatEvalWindowStart = 0;
float gHeatEvalStartTemp = NAN;
bool gCompletionHandled = false;
bool gPendingAlarmStatusPublish = false;

struct AutoTuneContext {
  bool active {false};
  bool riseCaptured {false};
  bool settleCaptured {false};
  bool unstable {false};
  uint32_t startedAtMs {0};
  uint32_t perturbStartMs {0};
  uint32_t settleStartMs {0};
  uint32_t settledAtMs {0};
  float baseTempC {NAN};
  float targetTempC {NAN};
  float peakTempC {-1000.0f};
  float candidateKp {0.0f};
  float candidateKi {0.0f};
  float candidateKd {0.0f};
};

AutoTuneContext gAutoTune;
constexpr float kAutoTuneHeaterMaxPct = 60.0f;
constexpr float kAutoTuneStepC = 2.0f;
constexpr uint32_t kAutoTuneMaxPerturbMs = 600000;
constexpr uint32_t kAutoTuneSettleBandMs = 20000;
constexpr float kAutoTuneSettleBandC = 0.3f;

static bool finitePositive(float v) {
  return isfinite(v) && v > 0.0f;
}

static float computeTuneQuality(float riseSec, float overshootC, float settlingSec, bool unstable) {
  if (unstable || !isfinite(riseSec) || !isfinite(overshootC) || !isfinite(settlingSec)) return 0.0f;
  const float risePenalty = min(40.0f, riseSec / 3.0f);
  const float settlePenalty = min(40.0f, settlingSec / 6.0f);
  const float overshootPenalty = min(20.0f, max(0.0f, overshootC) * 8.0f);
  return constrain(100.0f - (risePenalty + settlePenalty + overshootPenalty), 0.0f, 100.0f);
}

static bool candidateWithinGuardrails(float kp, float ki, float kd) {
  return finitePositive(kp) && finitePositive(ki) && finitePositive(kd) &&
         kp <= 60.0f && ki <= 2.0f && kd <= 80.0f;
}

static void applyTunings(float kp, float ki, float kd) {
  gPid.setTunings(kp, ki, kd);
  gPid.reset();
  gRt.currentKp = kp;
  gRt.currentKi = ki;
  gRt.currentKd = kd;
}

static void failAutoTuneAndRevert() {
  gAutoTune.active = false;
  gRt.autoTunePhase = AutoTunePhase::Failed;
  gRt.runState = RunState::Idle;
  gHeater.setMaxOutputPercent(100.0f);
  applyTunings(gCfg.pidKp, gCfg.pidKi, gCfg.pidKd);
}

static void startAutoTune() {
  if (!gRt.sensorHealthy || isnan(gRt.currentTempC)) return;
  gStages.stop();
  gCompletionHandled = false;
  gAutoTune = AutoTuneContext();
  gAutoTune.active = true;
  gAutoTune.startedAtMs = millis();
  gAutoTune.perturbStartMs = gAutoTune.startedAtMs;
  gAutoTune.baseTempC = gRt.currentTempC;
  gAutoTune.targetTempC = gAutoTune.baseTempC + kAutoTuneStepC;
  gAutoTune.peakTempC = gRt.currentTempC;
  gRt.autoTunePhase = AutoTunePhase::Perturbing;
  gRt.runState = RunState::AutoTune;
  gRt.autoTuneRiseTimeSec = 0.0f;
  gRt.autoTuneOvershootC = 0.0f;
  gRt.autoTuneSettlingSec = 0.0f;
  gRt.autoTuneQualityScore = 0.0f;
  gHeater.setMaxOutputPercent(kAutoTuneHeaterMaxPct);
}

static void finalizeAutoTuneCandidate() {
  const float riseSec = gRt.autoTuneRiseTimeSec > 0.0f ? gRt.autoTuneRiseTimeSec : 1.0f;
  const float settleSec = gRt.autoTuneSettlingSec > 0.0f ? gRt.autoTuneSettlingSec : riseSec * 1.5f;
  const float overshoot = max(0.0f, gRt.autoTuneOvershootC);

  gAutoTune.candidateKp = constrain(24.0f / riseSec, 2.0f, 45.0f);
  gAutoTune.candidateKi = constrain(gAutoTune.candidateKp / max(30.0f, settleSec), 0.01f, 1.5f);
  gAutoTune.candidateKd = constrain(gAutoTune.candidateKp * max(0.15f, overshoot), 0.5f, 60.0f);

  const bool invalidCandidate = !candidateWithinGuardrails(gAutoTune.candidateKp, gAutoTune.candidateKi, gAutoTune.candidateKd);
  gAutoTune.unstable = gAutoTune.unstable || invalidCandidate || overshoot > 2.5f;
  gRt.autoTuneQualityScore = computeTuneQuality(riseSec, overshoot, settleSec, gAutoTune.unstable);
  if (gAutoTune.unstable) {
    failAutoTuneAndRevert();
    return;
  }
  applyTunings(gAutoTune.candidateKp, gAutoTune.candidateKi, gAutoTune.candidateKd);
  gRt.autoTunePhase = AutoTunePhase::PendingAccept;
  gRt.runState = RunState::Idle;
  gAutoTune.active = false;
  gHeater.setMaxOutputPercent(100.0f);
}

static void updateAutoTune() {
  if (!gAutoTune.active) return;
  const uint32_t now = millis();
  const float temp = gRt.currentTempC;
  if (isnan(temp)) return;

  gAutoTune.peakTempC = max(gAutoTune.peakTempC, temp);
  gRt.heatingEnabled = true;

  if (gRt.autoTunePhase == AutoTunePhase::Perturbing) {
    gRt.heaterOutputPct = kAutoTuneHeaterMaxPct;
    gHeater.setEnabled(true);
    gHeater.setOutputPercent(gRt.heaterOutputPct);

    const float riseThreshold = gAutoTune.baseTempC + (kAutoTuneStepC * 0.9f);
    if (!gAutoTune.riseCaptured && temp >= riseThreshold) {
      gAutoTune.riseCaptured = true;
      gRt.autoTuneRiseTimeSec = (now - gAutoTune.perturbStartMs) / 1000.0f;
    }
    if (temp >= gAutoTune.targetTempC) {
      gRt.autoTuneOvershootC = max(0.0f, gAutoTune.peakTempC - gAutoTune.targetTempC);
      gRt.autoTunePhase = AutoTunePhase::Settling;
      gAutoTune.settleStartMs = now;
      gAutoTune.settledAtMs = 0;
      gRt.heaterOutputPct = 0.0f;
      gHeater.setOutputPercent(0.0f);
    } else if (now - gAutoTune.perturbStartMs > kAutoTuneMaxPerturbMs) {
      gAutoTune.unstable = true;
      failAutoTuneAndRevert();
    }
    return;
  }

  if (gRt.autoTunePhase == AutoTunePhase::Settling) {
    gRt.heaterOutputPct = 0.0f;
    gHeater.setEnabled(true);
    gHeater.setOutputPercent(0.0f);
    const bool inBand = fabsf(temp - gAutoTune.targetTempC) <= kAutoTuneSettleBandC;
    if (inBand) {
      if (gAutoTune.settledAtMs == 0) gAutoTune.settledAtMs = now;
      if (now - gAutoTune.settledAtMs >= kAutoTuneSettleBandMs) {
        gAutoTune.settleCaptured = true;
        gRt.autoTuneSettlingSec = (gAutoTune.settledAtMs - gAutoTune.settleStartMs) / 1000.0f;
      }
    } else {
      gAutoTune.settledAtMs = 0;
    }

    gRt.autoTuneOvershootC = max(gRt.autoTuneOvershootC, max(0.0f, gAutoTune.peakTempC - gAutoTune.targetTempC));
    if (gRt.autoTuneOvershootC > 3.0f) {
      gAutoTune.unstable = true;
      failAutoTuneAndRevert();
      return;
    }
    if (gAutoTune.settleCaptured || (now - gAutoTune.settleStartMs > 180000)) {
      if (!gAutoTune.settleCaptured) {
        gRt.autoTuneSettlingSec = (now - gAutoTune.settleStartMs) / 1000.0f;
      }
      finalizeAutoTuneCandidate();
    }
  }
}

static void debugLogBanner() {
  DBG_PRINTLN("\n=== BrewCore HLT V8 debug boot ===");
  DBG_PRINTF("DEBUG_ENABLED=%d WIFI=%d MQTT=%d\n",
             gDebugEnabled,
             !gDebugDisableWifi,
             !gDebugDisableMqtt);
}

static String maskSecret(const char* value) {
  if (!value || value[0] == '\0') return "<empty>";
  const size_t len = strlen(value);
  if (len <= 2) return "**";
  String out;
  out.reserve(len);
  for (size_t i = 0; i < len - 2; ++i) out += '*';
  out += value[len - 2];
  out += value[len - 1];
  return out;
}

static void debugPrintState(const char* tag) {
  DBG_PRINTF("[%s] run=%u ui=%u temp=%.2f sp=%.2f mins=%lu out=%.1f heat=%d wifi=%d mqtt=%d timer=%d alarm=%u\n",
             tag,
             (unsigned)gRt.runState,
             (unsigned)gRt.uiMode,
             gRt.currentTempC,
             gRt.currentSetpointC,
             (unsigned long)gRt.activeStageMinutes,
             gRt.heaterOutputPct,
             gRt.heatOn,
             gRt.wifiConnected,
             gRt.mqttConnected,
             gRt.stageTimerStarted,
             (unsigned)gRt.activeAlarm);
}

static const char* alarmText(AlarmCode code) {
  switch (code) {
    case AlarmCode::SensorFault: return "SENSOR FAULT";
    case AlarmCode::OverTemp: return "OVER TEMP";
    case AlarmCode::HeatingIneffective: return "NO TEMP RISE";
    case AlarmCode::MqttOffline: return "MQTT OFFLINE";
    default: return "OK";
  }
}

void syncAlarmFromManager() {
  gRt.activeAlarm = gAlarm.getAlarm();
  strlcpy(gRt.alarmText, gAlarm.getText(), sizeof(gRt.alarmText));
}

void clearFaultIfRecoverable() {
  if (gAlarm.getAlarm() == AlarmCode::SensorFault && gRt.sensorHealthy) gAlarm.clearAlarm();
  if (gAlarm.getAlarm() == AlarmCode::OverTemp && gRt.currentTempC < (gCfg.overTempC - 1.0f)) gAlarm.clearAlarm();
  if (gAlarm.getAlarm() == AlarmCode::HeatingIneffective && !isnan(gHeatEvalStartTemp) && gRt.currentTempC >= gHeatEvalStartTemp + Config::MIN_EXPECTED_RISE_C) {
    gAlarm.clearAlarm();
  }
  if (gAlarm.getAlarm() == AlarmCode::MqttOffline && gRt.mqttConnected) gAlarm.clearAlarm();
  syncAlarmFromManager();
}

static bool remoteCommsTimedOut(uint32_t now) {
  if (gCfg.mqttCommsTimeoutSec == 0) return false;
  if (gRt.controlMode != ControlMode::Remote) return false;
  if (gRt.runState != RunState::Running && gRt.runState != RunState::Paused) return false;
  if (gRt.mqttConnected) return false;

  const uint32_t lastCommsMs = max(gRt.lastValidMqttConnectionAtMs, gRt.lastAcceptedRemoteCommandAtMs);
  const uint32_t timeoutMs = gCfg.mqttCommsTimeoutSec * 1000UL;
  return (now - lastCommsMs) >= timeoutMs;
}

static void applyMqttTimeoutFallback() {
  switch (gCfg.mqttFallbackMode) {
    case MqttFallbackMode::HoldSetpoint:
      break;
    case MqttFallbackMode::Pause:
      gStages.pause();
      break;
    case MqttFallbackMode::StopHeater:
      gStages.stop();
      gCompletionHandled = false;
      gHeater.setEnabled(false);
      gHeater.setOutputPercent(0.0f);
      gRt.heatingEnabled = false;
      gRt.heaterOutputPct = 0.0f;
      break;
  }
}

void handleCommands(const char* topic, const char* payload) {
  DBG_PRINTF("MQTT cmd topic=%s payloadBytes=%u\n", topic, static_cast<unsigned>(strlen(payload)));
  String t(topic);
  JsonDocument doc;
  deserializeJson(doc, payload);
  bool accepted = false;

  if (t.endsWith("/cmd/setpoint")) {
    if (gCfg.controlLock != ControlLock::LocalOnly &&
        (gRt.uiMode == UiMode::SetpointAdjust || gRt.runState == RunState::Idle || gRt.runState == RunState::Complete)) {
      gRt.controlMode = ControlMode::Remote;
      gCfg.localSetpointC = doc["setpointC"] | atof(payload);
      gRt.currentSetpointC = gCfg.localSetpointC;
      gStorage.save(gCfg);
      gDisplay.invalidateAll();
      accepted = true;
    }
    gRt.controlMode = ControlMode::Remote;
    gCfg.localSetpointC = requestedSetpoint;
    gRt.currentSetpointC = gCfg.localSetpointC;
    gRt.desiredSetpointC = requestedSetpoint;
    needsStorageSave = true;
    needsDisplayRefresh = true;
    applied = true;
    reason = "applied";
  } else if (t.endsWith("/cmd/minutes")) {
    command = "minutes";
    int32_t mins = doc["minutes"] | atoi(payload);
    gRt.desiredMinutes = mins < 0 ? 0 : static_cast<uint32_t>(mins);
    accepted = true;
    if (mins < 0 || mins > 480) {
      applied = false;
      reason = "invalid_range_minutes";
      finishAck();
      return;
    }
    gCfg.manualStageMinutes = static_cast<uint32_t>(mins);
    gRt.activeStageMinutes = gCfg.manualStageMinutes;
    gStorage.save(gCfg);
    gDisplay.invalidateAll();
    accepted = true;
  } else if (t.endsWith("/cmd/start")) {
    command = "start";
    strlcpy(gRt.desiredRunAction, "start", sizeof(gRt.desiredRunAction));
    accepted = true;
    if (gRt.runState == RunState::Running || gRt.runState == RunState::AutoTune || gRt.runState == RunState::Fault) {
      applied = false;
      reason = "wrong_run_state";
      finishAck();
      return;
    }
    gStages.startProfile(0);
    gCompletionHandled = false;
    gDisplay.invalidateAll();
    accepted = true;
  } else if (t.endsWith("/cmd/pause")) {
    command = "pause";
    strlcpy(gRt.desiredRunAction, "pause", sizeof(gRt.desiredRunAction));
    accepted = true;
    if (gRt.runState != RunState::Running) {
      applied = false;
      reason = "wrong_run_state";
      finishAck();
      return;
    }
    gStages.pause();
    gDisplay.invalidateAll();
    accepted = true;
  } else if (t.endsWith("/cmd/stop")) {
    command = "stop";
    strlcpy(gRt.desiredRunAction, "stop", sizeof(gRt.desiredRunAction));
    accepted = true;
    if (gRt.runState == RunState::AutoTune) {
      applied = false;
      reason = "wrong_run_state";
      finishAck();
      return;
    }
    gStages.stop();
    gCompletionHandled = false;
    gDisplay.invalidateAll();
    accepted = true;
  } else if (t.endsWith("/cmd/reset_alarm")) {
    command = "reset_alarm";
    accepted = true;
    gAlarm.clearAlarm();
    syncAlarmFromManager();
    gDisplay.invalidateAll();
    accepted = true;
  } else if (t.endsWith("/cmd/start_autotune")) {
    command = "start_autotune";
    accepted = true;
    if (!gRt.sensorHealthy || isnan(gRt.currentTempC) || gRt.runState == RunState::Running || gRt.runState == RunState::Paused) {
      applied = false;
      reason = "wrong_run_state";
      finishAck();
      return;
    }
    startAutoTune();
    gDisplay.invalidateAll();
    accepted = true;
  } else if (t.endsWith("/cmd/accept_tune")) {
    command = "accept_tune";
    accepted = true;
    if (gRt.autoTunePhase != AutoTunePhase::PendingAccept) {
      applied = false;
      reason = "wrong_run_state";
      finishAck();
      return;
    }
    gDisplay.invalidateAll();
    accepted = true;
  }

  if (accepted) {
    gRt.lastAcceptedRemoteCommandAtMs = millis();
  }

  if (needsStorageSave) gStorage.save(gCfg);
  if (needsDisplayRefresh) gDisplay.invalidateAll();
  finishAck();
}

bool pointInRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < (rx + rw) && y >= ry && y < (ry + rh);
}

void handleTouch() {
  if (M5Dial.Touch.getCount() == 0) return;
  const auto t = M5Dial.Touch.getDetail(0);
  if (!t.wasPressed()) return;

  if (gRt.activeAlarm != AlarmCode::None && pointInRect(t.x, t.y, 40, 42, 160, 24)) {
    DBG_PRINTLN("Touch ack alarm");
    gAlarm.acknowledge();
    syncAlarmFromManager();
    gDisplay.invalidateAll();
    M5Dial.Speaker.tone(3200, 20);
  }
}

void handleButton() {
  if (M5Dial.BtnA.wasClicked()) {
    DBG_PRINTLN("Button clicked");
    switch (gRt.uiMode) {
      case UiMode::SetpointAdjust:
        gRt.uiMode = UiMode::StageTimeAdjust;
        break;
      case UiMode::StageTimeAdjust:
        gStages.startProfile(0);
        gCompletionHandled = false;
        break;
      case UiMode::Running:
        gStages.pause();
        break;
      case UiMode::Paused:
        gStages.resume();
        break;
    }
    gDisplay.invalidateAll();
  }

  if (M5Dial.BtnA.wasHold()) {
    DBG_PRINTLN("Button hold stop");
    gStages.stop();
    gCompletionHandled = false;
    gDisplay.invalidateAll();
    M5Dial.Speaker.tone(2200, 80);
  }
}

void processInput() {
  M5Dial.update();
  handleTouch();
  handleButton();

  static int32_t lastEnc = 0;
  const int32_t enc = M5Dial.Encoder.read();
  const int32_t diff = enc - lastEnc;
  if (diff == 0) return;
  lastEnc = enc;

  if (gRt.uiMode == UiMode::SetpointAdjust && gCfg.controlLock != ControlLock::RemoteOnly) {
    gRt.controlMode = ControlMode::Local;
    gCfg.localSetpointC = constrain(gCfg.localSetpointC + diff * 0.5f, 20.0f, 100.0f);
    gRt.currentSetpointC = gCfg.localSetpointC;
    if (gDebugVerboseInput) {
      DBG_PRINTF("Encoder setpoint diff=%ld localSetpoint=%.1f\n", (long)diff, gCfg.localSetpointC);
    }
  } else if (gRt.uiMode == UiMode::StageTimeAdjust) {
    int32_t mins = static_cast<int32_t>(gCfg.manualStageMinutes) + diff;
    if (mins < 0) mins = 0;
    if (mins > 480) mins = 480;
    gCfg.manualStageMinutes = static_cast<uint32_t>(mins);
    gRt.activeStageMinutes = gCfg.manualStageMinutes;
    if (gDebugVerboseInput) {
      DBG_PRINTF("Encoder minutes diff=%ld minutes=%lu\n", (long)diff, (unsigned long)gCfg.manualStageMinutes);
    }
  } else if (gDebugVerboseInput) {
    DBG_PRINTF("Encoder diff=%ld ignored ui=%u\n", (long)diff, (unsigned)gRt.uiMode);
  }

  gDisplay.requestImmediateUi();
}

void updateSafety() {
  gRt.sensorHealthy = gTempSensor.isHealthy();
  if (!gRt.sensorHealthy) {
    DBG_PRINTLN("Alarm: Sensor fault");
    gAlarm.setAlarm(AlarmCode::SensorFault, alarmText(AlarmCode::SensorFault));
    gRt.runState = RunState::Fault;
  } else if (gRt.currentTempC >= gCfg.overTempC) {
    DBG_PRINTLN("Alarm: Over temp");
    gAlarm.setAlarm(AlarmCode::OverTemp, alarmText(AlarmCode::OverTemp));
    gRt.runState = RunState::Fault;
  } else {
    clearFaultIfRecoverable();
    if (gRt.runState == RunState::Fault && gAlarm.getAlarm() == AlarmCode::None) {
      gRt.runState = RunState::Idle;
      gRt.uiMode = UiMode::SetpointAdjust;
    }
  }

  if (gHeater.getOutputPercent() > 20.0f && !isnan(gRt.currentTempC)) {
    if (gHeatEvalWindowStart == 0) {
      gHeatEvalWindowStart = millis();
      gHeatEvalStartTemp = gRt.currentTempC;
    } else if (millis() - gHeatEvalWindowStart >= Config::TEMP_RISE_EVAL_MS) {
      if (gRt.currentTempC < gHeatEvalStartTemp + Config::MIN_EXPECTED_RISE_C) {
        DBG_PRINTLN("Alarm: Heating ineffective");
        gAlarm.setAlarm(AlarmCode::HeatingIneffective, alarmText(AlarmCode::HeatingIneffective));
        gRt.runState = RunState::Fault;
      }
      gHeatEvalWindowStart = millis();
      gHeatEvalStartTemp = gRt.currentTempC;
    }
  } else {
    gHeatEvalWindowStart = 0;
    gHeatEvalStartTemp = NAN;
  }

  syncAlarmFromManager();
}

void updateControl() {
  const uint32_t now = millis();
  const float dt = (gLastPidMs == 0) ? 1.0f : (now - gLastPidMs) / 1000.0f;
  gLastPidMs = now;

  if (!gRt.sensorHealthy || gRt.runState == RunState::Fault) {
    gRt.heatingEnabled = false;
    gRt.heaterOutputPct = 0.0f;
    gPid.reset();
    gHeater.setEnabled(false);
    gHeater.setOutputPercent(0.0f);
    return;
  }

  if (gRt.runState == RunState::AutoTune) {
    updateAutoTune();
    return;
  }

  if (gRt.uiMode == UiMode::SetpointAdjust ||
      gRt.uiMode == UiMode::StageTimeAdjust ||
      gRt.runState == RunState::Idle ||
      gRt.runState == RunState::Complete) {
    gRt.currentSetpointC = gCfg.localSetpointC;
  }

  gRt.activeStageMinutes = gCfg.manualStageMinutes;

  if (gRt.runState == RunState::Running) {
    gStages.update(gRt.currentTempC);
  }

  if (gRt.runState == RunState::Paused || gRt.runState == RunState::Complete || gRt.uiMode == UiMode::StageTimeAdjust) {
    gRt.heatingEnabled = false;
    gRt.heaterOutputPct = 0.0f;
    gPid.reset();
    gHeater.setEnabled(false);
    gHeater.setOutputPercent(0.0f);
    return;
  }

  gRt.heatingEnabled = true;
  float pct = gPid.compute(gRt.currentSetpointC, gRt.currentTempC, dt);
  if (!isnan(gRt.currentTempC) && gRt.currentTempC >= gRt.currentSetpointC) pct = 0.0f;
  gRt.heaterOutputPct = pct;
  gHeater.setEnabled(true);
  gHeater.setOutputPercent(gRt.heaterOutputPct);
}

void setup() {
  DBG_BEGIN(115200);
  delay(50);
  debugLogBanner();

  auto cfg = M5.config();
  M5Dial.begin(cfg, true, false);
  M5Dial.Display.setRotation(0);

  gDisplay.begin();
  gStorage.begin();
  gStorage.load(gCfg);
  gRt.currentSetpointC = gCfg.localSetpointC;
  gRt.activeStageMinutes = gCfg.manualStageMinutes;
  gRt.desiredSetpointC = gCfg.localSetpointC;
  gRt.desiredMinutes = gCfg.manualStageMinutes;
  strlcpy(gRt.desiredRunAction, "stop", sizeof(gRt.desiredRunAction));
  gRt.controlMode = (gCfg.controlLock == ControlLock::RemoteOnly) ? ControlMode::Remote : ControlMode::Local;
  gRt.uiMode = UiMode::SetpointAdjust;
  gRt.lastValidMqttConnectionAtMs = millis();
  gRt.lastAcceptedRemoteCommandAtMs = gRt.lastValidMqttConnectionAtMs;

  gTempSensor.begin();
  applyTunings(gCfg.pidKp, gCfg.pidKi, gCfg.pidKd);
  gRt.previousKp = gCfg.prevPidKp;
  gRt.previousKi = gCfg.prevPidKi;
  gRt.previousKd = gCfg.prevPidKd;
  gRt.autoTuneQualityScore = gCfg.tuneQualityScore;
  gHeater.begin();
  gAlarm.begin();
  gStages.begin(&gCfg, &gRt);

  if (!gDebugDisableWifi) {
    gWifi.begin();
    DBG_PRINTF("WiFi commissioning AP: %s (pass=%s)\n",
               gWifi.getPortalApName(),
               maskSecret(gWifi.getPortalApPassword()).c_str());
  } else {
    DBG_PRINTLN("WiFi disabled by debug toggle");
    gRt.wifiConnected = false;
  }

  if (!gDebugDisableMqtt) {
    gMqtt.begin(&gCfg, &gRt);
    gMqtt.setCommandCallback(handleCommands);
  } else {
    DBG_PRINTLN("MQTT disabled by debug toggle");
    gRt.mqttConnected = false;
  }

  gDisplay.invalidateAll();
  debugPrintState("setup");
}

void loop() {
  processInput();
  const uint32_t now = millis();

  if (!gDebugDisableWifi) {
    gWifi.update();
    gRt.wifiConnected = gWifi.isConnected();
  } else {
    gRt.wifiConnected = false;
  }

  gTempSensor.update();
  if (gTempSensor.hasNewValue()) {
    gRt.currentTempC = gTempSensor.getCelsius();
    DBG_PRINTF("Temp update: %.2fC\n", gRt.currentTempC);
  }

  if (!gDebugDisableMqtt && now - gLastMqttServiceMs >= 50) {
    gLastMqttServiceMs = now;
    if (gRt.wifiConnected) gMqtt.update();
  } else if (gDebugDisableMqtt) {
    gRt.mqttConnected = false;
  }

  if (remoteCommsTimedOut(now) && gAlarm.getAlarm() != AlarmCode::MqttOffline) {
    gAlarm.setAlarm(AlarmCode::MqttOffline, alarmText(AlarmCode::MqttOffline));
    applyMqttTimeoutFallback();
    syncAlarmFromManager();
    gPendingAlarmStatusPublish = true;
    gDisplay.invalidateAll();
  }

  updateSafety();
  updateControl();
  gHeater.update();
  gRt.heatOn = gHeater.isOn();
  gAlarm.update();

  const BrewStage* stage = gStages.getCurrentStage();
  const uint32_t remaining = gStages.getRemainingSeconds();

  if (gRt.runState == RunState::Complete) {
    gHeater.setOutputPercent(0.0f);
    gHeater.setEnabled(false);
    gRt.heatOn = false;
    if (!gCompletionHandled) {
      DBG_PRINTLN("Stage complete");
      gAlarm.notifyStageComplete();
      gRt.pendingProfileCompletePublish = true;
      gCompletionHandled = true;
      gDisplay.invalidateAll();
    }
    if (gRt.mqttConnected && !gDebugDisableMqtt) gMqtt.publishProfileCompleteIfPending(gRt);
  } else {
    gCompletionHandled = false;
  }

  if (now - gLastStatusMs >= Config::STATUS_PUBLISH_MS) {
    gLastStatusMs = now;
    if (gRt.mqttConnected) {
      gMqtt.publishStatus(gRt, stage ? stage->name : "", remaining);
      if (!gDebugDisableMqtt) gMqtt.publishProfileCompleteIfPending(gRt);
    }
    gStorage.save(gCfg);
  }

  if (gPendingAlarmStatusPublish && gRt.mqttConnected) {
    gMqtt.publishStatus(gRt, stage ? stage->name : "", remaining);
    gPendingAlarmStatusPublish = false;
  }

  static uint32_t lastDebugStateMs = 0;
  if (gDebugEnabled && now - lastDebugStateMs >= 2000) {
    lastDebugStateMs = now;
    debugPrintState("loop");
  }

  gDisplay.draw(gCfg, gRt, stage, remaining);
  delay(1);
}
