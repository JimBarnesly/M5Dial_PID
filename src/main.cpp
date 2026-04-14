#include <Arduino.h>
#include <M5Dial.h>
#include <ArduinoJson.h>
#include <cmath>
#include <cstdlib>
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
#include "platform/m5dial/M5DialBuzzer.h"
#include "platform/m5dial/M5DialDigitalOut.h"

bool gDebugEnabled = true;
bool gDebugDisableWifi = false;
bool gDebugDisableMqtt = false;
bool gDebugVerboseInput = false;

#define DBG_BEGIN(...) do { if (gDebugEnabled) Serial.begin(__VA_ARGS__); } while (0)
#define DBG_PRINTLN(x) DBG_LOGLN(x)
#define DBG_PRINT(x) DBG_LOG(x)
#define DBG_PRINTF(...) DBG_LOGF(__VA_ARGS__)

PersistentConfig gCfg;
RuntimeState gRt;
TempSensor gTempSensor(Config::PIN_ONEWIRE);
PidController gPid;
HeaterOutput gHeater;
AlarmManager gAlarm;
M5DialBuzzer gBuzzer(Config::PIN_BUZZER);
M5DialDigitalOut gHeaterOut(Config::PIN_HEATER);
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
  DBG_PRINTLN("\n=== Env Controller debug boot ===");
  DBG_PRINTF("DEBUG_ENABLED=%d WIFI=%d MQTT=%d NET_MODE=%s RUNTIME_NET_TOGGLES=%d\n",
             gDebugEnabled,
             !debugWifiDisabledEffective(),
             !debugMqttDisabledEffective(),
             debugNetworkModeLabel(),
             debugRuntimeNetworkTogglesEnabled());
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

enum class SettingsField : uint8_t {
  OverTemp = 0,
  ControlLock,
  MqttTimeoutSec,
  MqttFallback,
  WifiPortalTimeoutSec,
  MqttHost,
  MqttPort,
  PidKp,
  PidKi,
  PidKd,
  Count
};

SettingsField gSettingsField = SettingsField::OverTemp;
int gSettingsHostOctet = 0;

static bool parseIpv4(const char* host, int octets[4]) {
  if (!host) return false;
  int consumed = 0;
  int o0 = 0, o1 = 0, o2 = 0, o3 = 0;
  if (sscanf(host, "%d.%d.%d.%d%n", &o0, &o1, &o2, &o3, &consumed) != 4) return false;
  if (host[consumed] != '\0') return false;
  if (o0 < 0 || o0 > 255 || o1 < 0 || o1 > 255 || o2 < 0 || o2 > 255 || o3 < 0 || o3 > 255) return false;
  octets[0] = o0; octets[1] = o1; octets[2] = o2; octets[3] = o3;
  return true;
}

static void formatIpv4(char* out, size_t outSize, const int octets[4]) {
  snprintf(out, outSize, "%d.%d.%d.%d", octets[0], octets[1], octets[2], octets[3]);
}

static void refreshSettingsUiText() {
  switch (gSettingsField) {
    case SettingsField::OverTemp:
      strlcpy(gRt.settingsLabel, "OVER TEMP C", sizeof(gRt.settingsLabel));
      snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%.1f", gCfg.overTempC);
      break;
    case SettingsField::ControlLock:
      strlcpy(gRt.settingsLabel, "CONTROL LOCK", sizeof(gRt.settingsLabel));
      if (gCfg.controlLock == ControlLock::LocalOnly) strlcpy(gRt.settingsValue, "LOCAL ONLY", sizeof(gRt.settingsValue));
      else if (gCfg.controlLock == ControlLock::RemoteOnly) strlcpy(gRt.settingsValue, "REMOTE ONLY", sizeof(gRt.settingsValue));
      else strlcpy(gRt.settingsValue, "LOCAL+REMOTE", sizeof(gRt.settingsValue));
      break;
    case SettingsField::MqttHost:
      strlcpy(gRt.settingsLabel, "MQTT HOST", sizeof(gRt.settingsLabel));
      snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%s [OCTET %d]", gCfg.mqttHost, gSettingsHostOctet + 1);
      break;
    case SettingsField::MqttTimeoutSec:
      strlcpy(gRt.settingsLabel, "MQTT TIMEOUT S", sizeof(gRt.settingsLabel));
      snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%u", gCfg.mqttCommsTimeoutSec);
      break;
    case SettingsField::MqttFallback:
      strlcpy(gRt.settingsLabel, "MQTT FALLBACK", sizeof(gRt.settingsLabel));
      if (gCfg.mqttFallbackMode == MqttFallbackMode::HoldSetpoint) strlcpy(gRt.settingsValue, "HOLD", sizeof(gRt.settingsValue));
      else if (gCfg.mqttFallbackMode == MqttFallbackMode::Pause) strlcpy(gRt.settingsValue, "PAUSE", sizeof(gRt.settingsValue));
      else strlcpy(gRt.settingsValue, "STOP HEATER", sizeof(gRt.settingsValue));
      break;
    case SettingsField::WifiPortalTimeoutSec:
      strlcpy(gRt.settingsLabel, "PORTAL TIMEOUT", sizeof(gRt.settingsLabel));
      snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%us", gCfg.wifiPortalTimeoutSec);
      break;
    case SettingsField::MqttPort:
      strlcpy(gRt.settingsLabel, "MQTT PORT", sizeof(gRt.settingsLabel));
      snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%u", gCfg.mqttPort);
      break;
    case SettingsField::PidKp:
      strlcpy(gRt.settingsLabel, "PID KP", sizeof(gRt.settingsLabel));
      snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%.2f", gCfg.pidKp);
      break;
    case SettingsField::PidKi:
      strlcpy(gRt.settingsLabel, "PID KI", sizeof(gRt.settingsLabel));
      snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%.3f", gCfg.pidKi);
      break;
    case SettingsField::PidKd:
      strlcpy(gRt.settingsLabel, "PID KD", sizeof(gRt.settingsLabel));
      snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%.2f", gCfg.pidKd);
      break;
    default:
      break;
  }
}

static void enterSettingsMode() {
  gRt.uiMode = UiMode::SettingsAdjust;
  gSettingsField = SettingsField::OverTemp;
  gSettingsHostOctet = 0;
  refreshSettingsUiText();
}

static void leaveSettingsMode() {
  gRt.uiMode = UiMode::SetpointAdjust;
  gRt.settingsLabel[0] = '\0';
  gRt.settingsValue[0] = '\0';
}

static void logRuntimeEvent(const char* text) {
  if (!text || text[0] == '\0') return;
  RuntimeEvent& ev = gRt.eventLog[gRt.eventLogHead];
  ev.atMs = millis();
  strlcpy(ev.text, text, sizeof(ev.text));
  gRt.eventLogHead = (gRt.eventLogHead + 1) % Config::EVENT_LOG_CAPACITY;
  if (gRt.eventLogCount < Config::EVENT_LOG_CAPACITY) ++gRt.eventLogCount;
  gRt.pendingEventLogPublish = true;
}

static void persistActivePidAndQuality() {
  gCfg.pidKp = gRt.currentKp;
  gCfg.pidKi = gRt.currentKi;
  gCfg.pidKd = gRt.currentKd;
  gCfg.prevPidKp = gRt.previousKp;
  gCfg.prevPidKi = gRt.previousKi;
  gCfg.prevPidKd = gRt.previousKd;
  gCfg.tuneQualityScore = gRt.autoTuneQualityScore;
  gStorage.save(gCfg);
}

static void applyWifiPortalNetworkConfig(bool persist, bool publishConfig) {
  const char* portalHost = gWifi.getConfiguredMqttHost();
  const uint16_t portalPort = gWifi.getConfiguredMqttPort();
  bool changed = false;

  if (portalHost && portalHost[0] != '\0' && strcmp(gCfg.mqttHost, portalHost) != 0) {
    strlcpy(gCfg.mqttHost, portalHost, sizeof(gCfg.mqttHost));
    changed = true;
  }
  if (gCfg.mqttPort != portalPort) {
    gCfg.mqttPort = portalPort;
    changed = true;
  }
  if (!changed) return;
  DBG_PRINTF("WiFi portal config changed mqttHost=%s mqttPort=%u\n", gCfg.mqttHost, gCfg.mqttPort);
  if (persist) gStorage.save(gCfg);
  if (publishConfig && gRt.mqttConnected) gMqtt.publishConfig(gCfg, gRt);
}

static bool shouldResetWifiFromBootHold() {
  const uint32_t holdWindowMs = 3000;
  const uint32_t started = millis();
  while (millis() - started < holdWindowMs) {
    M5Dial.update();
    if (!M5Dial.BtnA.isPressed()) return false;
    delay(10);
  }
  return true;
}

static bool upsertProfileFromJson(const JsonDocument& doc, uint8_t* outIndex = nullptr) {
  JsonObjectConst profileObj = doc["profile"].as<JsonObjectConst>();
  if (profileObj.isNull()) return false;

  int requestedIndex = profileObj["index"] | -1;
  uint8_t index = 0;
  if (requestedIndex >= 0) {
    if (requestedIndex >= Config::MAX_PROFILES) return false;
    index = static_cast<uint8_t>(requestedIndex);
    if (index >= gCfg.profileCount) gCfg.profileCount = index + 1;
  } else {
    if (gCfg.profileCount >= Config::MAX_PROFILES) return false;
    index = gCfg.profileCount++;
  }

  ProcessProfile& profile = gCfg.profiles[index];
  strlcpy(profile.name, profileObj["name"] | "PROFILE", sizeof(profile.name));
  profile.stageCount = 0;
  JsonArrayConst stages = profileObj["stages"].as<JsonArrayConst>();
  if (stages.isNull()) return false;
  for (JsonObjectConst s : stages) {
    if (profile.stageCount >= Config::MAX_STAGES) break;
    ProcessStage& stage = profile.stages[profile.stageCount];
    strlcpy(stage.name, s["name"] | "STAGE", sizeof(stage.name));
    const float target = s["targetC"] | NAN;
    const uint32_t hold = s["holdSeconds"] | 0UL;
    if (!isfinite(target) || target < 20.0f || target > 120.0f) continue;
    stage.targetC = target;
    stage.holdSeconds = hold;
    ++profile.stageCount;
  }
  if (profile.stageCount == 0) return false;
  if (gCfg.activeProfileIndex >= gCfg.profileCount) gCfg.activeProfileIndex = 0;
  if (outIndex) *outIndex = index;
  return true;
}

void syncAlarmFromManager() {
  gRt.activeAlarm = gAlarm.getAlarm();
  gRt.alarmAcknowledged = gAlarm.isAcknowledged();
  strlcpy(gRt.alarmText, gAlarm.getText(), sizeof(gRt.alarmText));
}

void clearFaultIfRecoverable() {
  if (gAlarm.getAlarm() == AlarmCode::SensorFault && gRt.sensorHealthy) gAlarm.clearAlarm(AlarmControlSource::System);
  if (gAlarm.getAlarm() == AlarmCode::OverTemp && gRt.currentTempC < (gCfg.overTempC - 1.0f)) gAlarm.clearAlarm(AlarmControlSource::System);
  if (gAlarm.getAlarm() == AlarmCode::HeatingIneffective && !isnan(gHeatEvalStartTemp) && gRt.currentTempC >= gHeatEvalStartTemp + Config::MIN_EXPECTED_RISE_C) {
    gAlarm.clearAlarm(AlarmControlSource::System);
  }
  if (gAlarm.getAlarm() == AlarmCode::MqttOffline && gRt.mqttConnected) gAlarm.clearAlarm(AlarmControlSource::System);
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
  const char* command = "unknown";
  bool accepted = false;
  bool applied = true;
  const char* reason = "ok";
  const char* cmdId = doc["cmdId"] | "";
  const bool controlLockedLocalOnly = gCfg.controlLock == ControlLock::LocalOnly;

  auto parsePayloadFloat = [&](float fallback) -> float {
    char* endPtr = nullptr;
    const float parsed = strtof(payload, &endPtr);
    if (endPtr != payload && endPtr && *endPtr == '\0' && isfinite(parsed)) return parsed;
    return fallback;
  };

  auto parsePayloadInt = [&](int fallback) -> int {
    char* endPtr = nullptr;
    const long parsed = strtol(payload, &endPtr, 10);
    if (endPtr != payload && endPtr && *endPtr == '\0') return static_cast<int>(parsed);
    return fallback;
  };

  auto finishAck = [&]() {
    gMqtt.publishCommandAck(cmdId,
                            command,
                            accepted,
                            applied,
                            reason,
                            gRt,
                            gStages.getRemainingSeconds());
  };

  if (t.endsWith("/cmd/setpoint")) {
    command = "setpoint";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else if (gRt.uiMode == UiMode::SetpointAdjust || gRt.runState == RunState::Idle || gRt.runState == RunState::Complete) {
      const float requested = doc["setpointC"] | parsePayloadFloat(NAN);
      if (!isfinite(requested) || requested < 20.0f || requested > 100.0f) {
        applied = false;
        reason = "invalid_range_setpoint";
      } else {
        gRt.controlMode = ControlMode::Remote;
        gCfg.localSetpointC = requested;
        gRt.currentSetpointC = gCfg.localSetpointC;
        gStorage.save(gCfg);
        gDisplay.invalidateAll();
      }
    } else {
      applied = false;
      reason = "wrong_run_state";
    }
  } else if (t.endsWith("/cmd/over_temp")) {
    command = "over_temp";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      const float v = doc["overTempC"] | parsePayloadFloat(NAN);
      if (isfinite(v) && v >= 20.0f && v <= 140.0f) {
        gCfg.overTempC = v;
        gStorage.save(gCfg);
        if (gRt.mqttConnected) gMqtt.publishConfig(gCfg, gRt);
        gDisplay.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_range_over_temp";
      }
    }
  } else if (t.endsWith("/cmd/control_lock")) {
    command = "control_lock";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      int lockRaw = doc["controlLock"] | parsePayloadInt(-1);
      if (lockRaw >= 0 && lockRaw <= 2) {
        gCfg.controlLock = static_cast<ControlLock>(lockRaw);
        if (gCfg.controlLock == ControlLock::RemoteOnly) gRt.controlMode = ControlMode::Remote;
        else if (gRt.controlMode == ControlMode::Remote) gRt.controlMode = ControlMode::Local;
        gStorage.save(gCfg);
        if (gRt.mqttConnected) gMqtt.publishConfig(gCfg, gRt);
        gDisplay.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_control_lock";
      }
    }
  } else if (t.endsWith("/cmd/mqtt_host")) {
    command = "mqtt_host";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      const char* host = doc["host"] | payload;
      if (host && strlen(host) > 0 && strlen(host) < sizeof(gCfg.mqttHost)) {
        strlcpy(gCfg.mqttHost, host, sizeof(gCfg.mqttHost));
        gStorage.save(gCfg);
        if (gRt.mqttConnected) gMqtt.publishConfig(gCfg, gRt);
        gDisplay.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_mqtt_host";
      }
    }
  } else if (t.endsWith("/cmd/mqtt_port")) {
    command = "mqtt_port";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      int port = doc["port"] | parsePayloadInt(-1);
      if (port > 0 && port <= 65535) {
        gCfg.mqttPort = static_cast<uint16_t>(port);
        gStorage.save(gCfg);
        if (gRt.mqttConnected) gMqtt.publishConfig(gCfg, gRt);
        gDisplay.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_mqtt_port";
      }
    }
  } else if (t.endsWith("/cmd/mqtt_timeout")) {
    command = "mqtt_timeout";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      const int timeout = doc["seconds"] | parsePayloadInt(-1);
      if (timeout >= 0 && timeout <= 3600) {
        gCfg.mqttCommsTimeoutSec = static_cast<uint16_t>(timeout);
        gStorage.save(gCfg);
        if (gRt.mqttConnected) gMqtt.publishConfig(gCfg, gRt);
        gDisplay.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_mqtt_timeout";
      }
    }
  } else if (t.endsWith("/cmd/mqtt_fallback")) {
    command = "mqtt_fallback";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      int mode = doc["mode"] | parsePayloadInt(-1);
      if (mode >= static_cast<int>(MqttFallbackMode::HoldSetpoint) &&
          mode <= static_cast<int>(MqttFallbackMode::StopHeater)) {
        gCfg.mqttFallbackMode = static_cast<MqttFallbackMode>(mode);
        gStorage.save(gCfg);
        if (gRt.mqttConnected) gMqtt.publishConfig(gCfg, gRt);
        gDisplay.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_mqtt_fallback";
      }
    }
  } else if (t.endsWith("/cmd/wifi_portal_timeout")) {
    command = "wifi_portal_timeout";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      const int timeout = doc["seconds"] | parsePayloadInt(-1);
      if (timeout >= 30 && timeout <= 1800) {
        gCfg.wifiPortalTimeoutSec = static_cast<uint16_t>(timeout);
        gStorage.save(gCfg);
        if (gRt.mqttConnected) gMqtt.publishConfig(gCfg, gRt);
        gDisplay.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_wifi_timeout";
      }
    }
  } else if (t.endsWith("/cmd/reset_wifi")) {
    command = "reset_wifi";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      gWifi.resetSettings();
      logRuntimeEvent("WiFi settings reset (remote)");
    }
  } else if (t.endsWith("/cmd/pid")) {
    command = "pid";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      const float kp = doc["kp"] | gCfg.pidKp;
      const float ki = doc["ki"] | gCfg.pidKi;
      const float kd = doc["kd"] | gCfg.pidKd;
      if (candidateWithinGuardrails(kp, ki, kd)) {
        gCfg.pidKp = kp;
        gCfg.pidKi = ki;
        gCfg.pidKd = kd;
        applyTunings(gCfg.pidKp, gCfg.pidKi, gCfg.pidKd);
        gStorage.save(gCfg);
        if (gRt.mqttConnected) gMqtt.publishConfig(gCfg, gRt);
        gDisplay.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_pid";
      }
    }
  } else if (t.endsWith("/cmd/pid_kp")) {
    command = "pid_kp";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      const float kp = doc["kp"] | parsePayloadFloat(NAN);
      if (finitePositive(kp) && kp <= 60.0f) {
        gCfg.pidKp = kp;
        applyTunings(gCfg.pidKp, gCfg.pidKi, gCfg.pidKd);
        gStorage.save(gCfg);
        if (gRt.mqttConnected) gMqtt.publishConfig(gCfg, gRt);
        gDisplay.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_pid_kp";
      }
    }
  } else if (t.endsWith("/cmd/pid_ki")) {
    command = "pid_ki";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      const float ki = doc["ki"] | parsePayloadFloat(NAN);
      if (finitePositive(ki) && ki <= 2.0f) {
        gCfg.pidKi = ki;
        applyTunings(gCfg.pidKp, gCfg.pidKi, gCfg.pidKd);
        gStorage.save(gCfg);
        if (gRt.mqttConnected) gMqtt.publishConfig(gCfg, gRt);
        gDisplay.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_pid_ki";
      }
    }
  } else if (t.endsWith("/cmd/pid_kd")) {
    command = "pid_kd";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      const float kd = doc["kd"] | parsePayloadFloat(NAN);
      if (finitePositive(kd) && kd <= 80.0f) {
        gCfg.pidKd = kd;
        applyTunings(gCfg.pidKp, gCfg.pidKi, gCfg.pidKd);
        gStorage.save(gCfg);
        if (gRt.mqttConnected) gMqtt.publishConfig(gCfg, gRt);
        gDisplay.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_pid_kd";
      }
    }
  } else if (t.endsWith("/cmd/get_config")) {
    command = "get_config";
    accepted = true;
    if (gRt.mqttConnected) gMqtt.publishConfig(gCfg, gRt);
  } else if (t.endsWith("/cmd/get_events")) {
    command = "get_events";
    accepted = true;
    if (gRt.mqttConnected) gMqtt.publishEventLog(gRt);
  } else if (t.endsWith("/cmd/profile_select")) {
    command = "profile_select";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    const int index = doc["index"] | -1;
    if (index < 0 || index >= gCfg.profileCount) {
      applied = false;
      reason = "invalid_profile_index";
      finishAck();
      return;
    }
    gCfg.activeProfileIndex = static_cast<uint8_t>(index);
    gStorage.save(gCfg);
    logRuntimeEvent("Profile selected");
    gDisplay.invalidateAll();
  } else if (t.endsWith("/cmd/profile_start")) {
    command = "profile_start";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    const int index = doc["index"] | static_cast<int>(gCfg.activeProfileIndex);
    if (index < 0 || index >= gCfg.profileCount) {
      applied = false;
      reason = "invalid_profile_index";
      finishAck();
      return;
    }
    gCfg.activeProfileIndex = static_cast<uint8_t>(index);
    gStages.startProfile(gCfg.activeProfileIndex);
    gCompletionHandled = false;
    logRuntimeEvent("Profile run started");
    gStorage.save(gCfg);
    gDisplay.invalidateAll();
  } else if (t.endsWith("/cmd/profile_delete")) {
    command = "profile_delete";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    const int index = doc["index"] | -1;
    if (index < 0 || index >= gCfg.profileCount) {
      applied = false;
      reason = "invalid_profile_index";
      finishAck();
      return;
    }
    for (int i = index; i < gCfg.profileCount - 1; ++i) {
      gCfg.profiles[i] = gCfg.profiles[i + 1];
    }
    if (gCfg.profileCount > 0) --gCfg.profileCount;
    if (gCfg.profileCount == 0) gCfg.activeProfileIndex = 0;
    else if (gCfg.activeProfileIndex >= gCfg.profileCount) gCfg.activeProfileIndex = gCfg.profileCount - 1;
    gStorage.save(gCfg);
    logRuntimeEvent("Profile deleted");
    gDisplay.invalidateAll();
  } else if (t.endsWith("/cmd/profile_upsert")) {
    command = "profile_upsert";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    uint8_t outIndex = 0;
    if (!upsertProfileFromJson(doc, &outIndex)) {
      applied = false;
      reason = "invalid_profile_payload";
      finishAck();
      return;
    }
    gStorage.save(gCfg);
    if (gRt.mqttConnected) gMqtt.publishConfig(gCfg, gRt);
    logRuntimeEvent("Profile upserted");
    gDisplay.invalidateAll();
  } else if (t.endsWith("/cmd/minutes")) {
    command = "minutes";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    const int32_t mins = doc["minutes"] | parsePayloadInt(-1);
    if (mins < 0 || mins > 480) {
      applied = false;
      reason = "invalid_range_minutes";
      finishAck();
      return;
    }
    gRt.desiredMinutes = static_cast<uint32_t>(mins);
    gCfg.manualStageMinutes = static_cast<uint32_t>(mins);
    gRt.activeStageMinutes = gCfg.manualStageMinutes;
    gStorage.save(gCfg);
    gDisplay.invalidateAll();
  } else if (t.endsWith("/cmd/start")) {
    command = "start";
    strlcpy(gRt.desiredRunAction, "start", sizeof(gRt.desiredRunAction));
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    if (gRt.runState == RunState::Running || gRt.runState == RunState::AutoTune || gRt.runState == RunState::Fault) {
      applied = false;
      reason = "wrong_run_state";
      finishAck();
      return;
    }
    gStages.start();
    gCompletionHandled = false;
    logRuntimeEvent("Run started");
    gDisplay.invalidateAll();
  } else if (t.endsWith("/cmd/pause")) {
    command = "pause";
    strlcpy(gRt.desiredRunAction, "pause", sizeof(gRt.desiredRunAction));
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    if (gRt.runState != RunState::Running) {
      applied = false;
      reason = "wrong_run_state";
      finishAck();
      return;
    }
    gStages.pause();
    logRuntimeEvent("Run paused");
    gDisplay.invalidateAll();
  } else if (t.endsWith("/cmd/stop")) {
    command = "stop";
    strlcpy(gRt.desiredRunAction, "stop", sizeof(gRt.desiredRunAction));
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    if (gRt.runState == RunState::AutoTune) {
      applied = false;
      reason = "wrong_run_state";
      finishAck();
      return;
    }
    gStages.stop();
    gCompletionHandled = false;
    logRuntimeEvent("Run stopped");
    gDisplay.invalidateAll();
  } else if (t.endsWith("/cmd/reset_alarm")) {
    command = "reset_alarm";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    gAlarm.clearAlarm(AlarmControlSource::RemoteMqtt);
    syncAlarmFromManager();
    logRuntimeEvent("Alarm reset (remote)");
    gDisplay.invalidateAll();
  } else if (t.endsWith("/cmd/ack_alarm")) {
    command = "ack_alarm";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    if (!gAlarm.acknowledge(AlarmControlSource::RemoteMqtt)) {
      applied = false;
      reason = "ack_not_allowed";
      finishAck();
      return;
    }
    syncAlarmFromManager();
    logRuntimeEvent("Alarm acknowledged (remote)");
    gDisplay.invalidateAll();
  } else if (t.endsWith("/cmd/start_autotune")) {
    command = "start_autotune";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    if (!gRt.sensorHealthy || isnan(gRt.currentTempC) || gRt.runState == RunState::Running || gRt.runState == RunState::Paused) {
      applied = false;
      reason = "wrong_run_state";
      finishAck();
      return;
    }
    startAutoTune();
    logRuntimeEvent("Autotune started");
    gDisplay.invalidateAll();
  } else if (t.endsWith("/cmd/accept_tune")) {
    command = "accept_tune";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    if (gRt.autoTunePhase != AutoTunePhase::PendingAccept) {
      applied = false;
      reason = "wrong_run_state";
      finishAck();
      return;
    }
    gRt.previousKp = gCfg.pidKp;
    gRt.previousKi = gCfg.pidKi;
    gRt.previousKd = gCfg.pidKd;
    persistActivePidAndQuality();
    gRt.autoTunePhase = AutoTunePhase::Complete;
    logRuntimeEvent("Autotune accepted");
    if (gRt.mqttConnected) gMqtt.publishConfig(gCfg, gRt);
    gDisplay.invalidateAll();
  } else if (t.endsWith("/cmd/reject_tune")) {
    command = "reject_tune";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    if (gRt.autoTunePhase != AutoTunePhase::PendingAccept) {
      applied = false;
      reason = "wrong_run_state";
      finishAck();
      return;
    }
    applyTunings(gCfg.pidKp, gCfg.pidKi, gCfg.pidKd);
    gRt.autoTunePhase = AutoTunePhase::Failed;
    gRt.autoTuneQualityScore = 0.0f;
    logRuntimeEvent("Autotune rejected");
    gDisplay.invalidateAll();
  } else if (t.endsWith("/cmd/temp_calibration")) {
    command = "temp_calibration";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    const float requestedOffset = doc["tempOffsetC"] | parsePayloadFloat(gCfg.tempOffsetC);
    const bool hasSmoothing = !doc["tempSmoothingAlpha"].isNull();
    const bool hasOffset = !doc["tempOffsetC"].isNull();

    gCfg.tempOffsetC = constrain(hasOffset ? requestedOffset : gCfg.tempOffsetC, -10.0f, 10.0f);
    if (hasSmoothing) {
      gCfg.tempSmoothingAlpha = constrain(static_cast<float>(doc["tempSmoothingAlpha"]), 0.0f, 1.0f);
    }

    gTempSensor.setCalibrationOffset(gCfg.tempOffsetC);
    gTempSensor.setSmoothingFactor(gCfg.tempSmoothingAlpha);
    gStorage.save(gCfg);
    if (gRt.mqttConnected) gMqtt.publishCalibrationStatus(gCfg, gRt);
  } else if (t.endsWith("/cmd/calibration_status")) {
    command = "calibration_status";
    accepted = true;
    if (gRt.mqttConnected) gMqtt.publishCalibrationStatus(gCfg, gRt);
  }

  if (accepted) {
    gRt.lastAcceptedRemoteCommandAtMs = millis();
  }

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
    DBG_PRINTLN("Touch reset alarm");
    gAlarm.clearAlarm(AlarmControlSource::LocalUi);
    syncAlarmFromManager();
    gDisplay.invalidateAll();
    M5Dial.Speaker.tone(3200, 20);
  }
}

void handleButton() {
  if (M5Dial.BtnA.wasClicked()) {
    DBG_PRINTLN("Button clicked");
    if (gRt.autoTunePhase == AutoTunePhase::PendingAccept) {
      gRt.previousKp = gCfg.pidKp;
      gRt.previousKi = gCfg.pidKi;
      gRt.previousKd = gCfg.pidKd;
      persistActivePidAndQuality();
      gRt.autoTunePhase = AutoTunePhase::Complete;
      logRuntimeEvent("Autotune accepted (local)");
      if (gRt.mqttConnected) gMqtt.publishConfig(gCfg, gRt);
      gDisplay.invalidateAll();
      return;
    }
    if (gRt.uiMode == UiMode::SettingsAdjust) {
      gSettingsField = static_cast<SettingsField>((static_cast<uint8_t>(gSettingsField) + 1) %
                                                  static_cast<uint8_t>(SettingsField::Count));
      refreshSettingsUiText();
      gDisplay.invalidateAll();
      return;
    }

    switch (gRt.uiMode) {
      case UiMode::SetpointAdjust:
        gRt.uiMode = UiMode::StageTimeAdjust;
        break;
      case UiMode::StageTimeAdjust:
        gStages.start();
        gCompletionHandled = false;
        break;
      case UiMode::Running:
        gStages.pause();
        break;
      case UiMode::Paused:
        gStages.resume();
        break;
      case UiMode::SettingsAdjust:
        break;
    }
    gDisplay.invalidateAll();
  }

  if (M5Dial.BtnA.wasHold()) {
    DBG_PRINTLN("Button hold stop");
    if (gRt.autoTunePhase == AutoTunePhase::PendingAccept) {
      applyTunings(gCfg.pidKp, gCfg.pidKi, gCfg.pidKd);
      gRt.autoTunePhase = AutoTunePhase::Failed;
      gRt.autoTuneQualityScore = 0.0f;
      logRuntimeEvent("Autotune rejected (local)");
      gDisplay.invalidateAll();
      M5Dial.Speaker.tone(2200, 80);
      return;
    }
    if (gRt.uiMode == UiMode::SettingsAdjust) {
      leaveSettingsMode();
      gDisplay.invalidateAll();
      M5Dial.Speaker.tone(2200, 80);
      return;
    }
    gStages.stop();
    gCompletionHandled = false;
    gDisplay.invalidateAll();
    M5Dial.Speaker.tone(2200, 80);
  }
}

void processInput() {
  M5Dial.update();
  handleTouch();

  if (gRt.activeAlarm != AlarmCode::None && gDisplay.wasAlarmPillTouched()) {
    DBG_PRINTLN("Display alarm-pill reset");
    gAlarm.clearAlarm(AlarmControlSource::LocalUi);
    syncAlarmFromManager();
    gDisplay.invalidateAll();
    M5Dial.Speaker.tone(3200, 20);
  }

  if (gDisplay.wasSettingsTouched() && gRt.runState != RunState::Running && gRt.runState != RunState::Paused) {
    if (gRt.uiMode == UiMode::SettingsAdjust) leaveSettingsMode();
    else enterSettingsMode();
    gDisplay.invalidateAll();
  }

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
  } else if (gRt.uiMode == UiMode::SettingsAdjust) {
    bool changed = false;
    if (gSettingsField == SettingsField::OverTemp) {
      gCfg.overTempC = constrain(gCfg.overTempC + diff * 0.5f, 20.0f, 140.0f);
      changed = true;
    } else if (gSettingsField == SettingsField::ControlLock) {
      int lock = static_cast<int>(gCfg.controlLock) + (diff > 0 ? 1 : -1);
      while (lock < 0) lock += 3;
      while (lock > 2) lock -= 3;
      gCfg.controlLock = static_cast<ControlLock>(lock);
      changed = true;
    } else if (gSettingsField == SettingsField::MqttTimeoutSec) {
      int nextTimeout = static_cast<int>(gCfg.mqttCommsTimeoutSec) + static_cast<int>(diff);
      gCfg.mqttCommsTimeoutSec = static_cast<uint16_t>(constrain(nextTimeout, 0, 3600));
      changed = true;
    } else if (gSettingsField == SettingsField::MqttFallback) {
      int nextMode = static_cast<int>(gCfg.mqttFallbackMode) + (diff > 0 ? 1 : -1);
      while (nextMode < static_cast<int>(MqttFallbackMode::HoldSetpoint)) nextMode += 3;
      while (nextMode > static_cast<int>(MqttFallbackMode::StopHeater)) nextMode -= 3;
      gCfg.mqttFallbackMode = static_cast<MqttFallbackMode>(nextMode);
      changed = true;
    } else if (gSettingsField == SettingsField::WifiPortalTimeoutSec) {
      int nextTimeout = static_cast<int>(gCfg.wifiPortalTimeoutSec) + static_cast<int>(diff * 5);
      gCfg.wifiPortalTimeoutSec = static_cast<uint16_t>(constrain(nextTimeout, 30, 1800));
      changed = true;
    } else if (gSettingsField == SettingsField::MqttHost) {
      int octets[4] = {192, 168, 1, 10};
      parseIpv4(gCfg.mqttHost, octets);
      if (M5Dial.BtnA.isPressed()) {
        gSettingsHostOctet = (gSettingsHostOctet + (diff > 0 ? 1 : -1) + 4) % 4;
      } else {
        octets[gSettingsHostOctet] = constrain(octets[gSettingsHostOctet] + static_cast<int>(diff), 0, 255);
        char hostBuf[64];
        formatIpv4(hostBuf, sizeof(hostBuf), octets);
        strlcpy(gCfg.mqttHost, hostBuf, sizeof(gCfg.mqttHost));
        changed = true;
      }
    } else if (gSettingsField == SettingsField::MqttPort) {
      int nextPort = static_cast<int>(gCfg.mqttPort) + static_cast<int>(diff);
      gCfg.mqttPort = static_cast<uint16_t>(constrain(nextPort, 1, 65535));
      changed = true;
    } else if (gSettingsField == SettingsField::PidKp) {
      gCfg.pidKp = constrain(gCfg.pidKp + diff * 0.2f, 0.1f, 60.0f);
      changed = true;
    } else if (gSettingsField == SettingsField::PidKi) {
      gCfg.pidKi = constrain(gCfg.pidKi + diff * 0.01f, 0.001f, 2.0f);
      changed = true;
    } else if (gSettingsField == SettingsField::PidKd) {
      gCfg.pidKd = constrain(gCfg.pidKd + diff * 0.2f, 0.1f, 80.0f);
      changed = true;
    }

    if (changed) {
      if (gCfg.controlLock == ControlLock::RemoteOnly) gRt.controlMode = ControlMode::Remote;
      else if (gRt.controlMode == ControlMode::Remote) gRt.controlMode = ControlMode::Local;
      applyTunings(gCfg.pidKp, gCfg.pidKi, gCfg.pidKd);
      gStorage.save(gCfg);
      if (gRt.mqttConnected) gMqtt.publishConfig(gCfg, gRt);
    }
    refreshSettingsUiText();
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
    logRuntimeEvent("Alarm: Sensor fault");
    gRt.runState = RunState::Fault;
  } else if (gRt.currentTempC >= gCfg.overTempC) {
    DBG_PRINTLN("Alarm: Over temp");
    gAlarm.setAlarm(AlarmCode::OverTemp, alarmText(AlarmCode::OverTemp));
    logRuntimeEvent("Alarm: Over temp");
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
        logRuntimeEvent("Alarm: Heating ineffective");
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
      gRt.uiMode == UiMode::SettingsAdjust ||
      gRt.runState == RunState::Idle ||
      gRt.runState == RunState::Complete) {
    gRt.currentSetpointC = gCfg.localSetpointC;
  }

  gRt.activeStageMinutes = gCfg.manualStageMinutes;

  if (gRt.runState == RunState::Running) {
    gStages.update(gRt.currentTempC);
  }

  if (gRt.runState == RunState::Paused || gRt.runState == RunState::Complete ||
      gRt.uiMode == UiMode::StageTimeAdjust || gRt.uiMode == UiMode::SettingsAdjust) {
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
  gTempSensor.setCalibrationOffset(gCfg.tempOffsetC);
  gTempSensor.setSmoothingFactor(gCfg.tempSmoothingAlpha);
  gTempSensor.setPlausibilityLimit(Config::DEFAULT_TEMP_MAX_RATE_C_PER_SEC);
  applyTunings(gCfg.pidKp, gCfg.pidKi, gCfg.pidKd);
  gRt.previousKp = gCfg.prevPidKp;
  gRt.previousKi = gCfg.prevPidKi;
  gRt.previousKd = gCfg.prevPidKd;
  gRt.autoTuneQualityScore = gCfg.tuneQualityScore;
  gHeaterOut.begin();
  gHeater.setActiveHigh(Config::HEATER_ACTIVE_HIGH);
  gHeater.setDriveHandler([](bool on) { gHeaterOut.set(on); });
  gHeater.begin();
  gBuzzer.begin();
  gAlarm.setSignalHandler([](bool on) { gBuzzer.set(on); });
  gAlarm.begin();
  gAlarm.setLocalUiAlarmControlEnabled(true);
  gStages.begin(&gCfg, &gRt);

  if (!debugWifiDisabledEffective()) {
    if (shouldResetWifiFromBootHold()) {
      gWifi.resetSettings();
      logRuntimeEvent("WiFi settings reset (local boot hold)");
    }
    gWifi.begin(gCfg.wifiPortalTimeoutSec, gCfg.mqttHost, gCfg.mqttPort);
    DBG_PRINTF("WiFi begin done mqttHost=%s mqttPort=%u timeout=%u\n",
               gCfg.mqttHost,
               gCfg.mqttPort,
               static_cast<unsigned>(gCfg.wifiPortalTimeoutSec));
    if (gWifi.hasPendingConfigUpdate()) {
      applyWifiPortalNetworkConfig(true, false);
      gWifi.clearPendingConfigUpdate();
    }
    DBG_PRINTF("WiFi commissioning AP: %s (pass=%s)\n",
               gWifi.getPortalApName(),
               maskSecret(gWifi.getPortalApPassword()).c_str());
  } else {
    DBG_LOGLN("WiFi disabled by debug/compile-time network mode");
    gRt.wifiConnected = false;
  }

  if (!debugMqttDisabledEffective()) {
    gMqtt.begin(&gCfg, &gRt);
    gMqtt.setCommandCallback(handleCommands);
  } else {
    DBG_LOGLN("MQTT disabled by debug/compile-time network mode");
    gRt.mqttConnected = false;
  }

  gDisplay.invalidateAll();
  logRuntimeEvent("System booted");
  debugPrintState("setup");
}

void loop() {
  processInput();
  const uint32_t now = millis();

  if (!debugWifiDisabledEffective()) {
    gWifi.update();
    if (gWifi.hasPendingConfigUpdate()) {
      applyWifiPortalNetworkConfig(true, true);
      gWifi.clearPendingConfigUpdate();
    }
    gRt.wifiConnected = gWifi.isConnected();
  } else {
    gRt.wifiConnected = false;
  }

  gTempSensor.update();
  gRt.currentRawTempC = gTempSensor.getRawCelsius();
  gRt.tempPlausible = gTempSensor.isPlausible();
  if (gTempSensor.hasNewValue()) {
    gRt.currentTempC = gTempSensor.getCelsius();
    DBG_PRINTF("Temp update: %.2fC\n", gRt.currentTempC);
  }

  if (!debugMqttDisabledEffective() && now - gLastMqttServiceMs >= 50) {
    gLastMqttServiceMs = now;
    if (gRt.wifiConnected) gMqtt.update();
  } else if (debugMqttDisabledEffective()) {
    gRt.mqttConnected = false;
  }

  if (remoteCommsTimedOut(now) && gAlarm.getAlarm() != AlarmCode::MqttOffline) {
    gAlarm.setAlarm(AlarmCode::MqttOffline, alarmText(AlarmCode::MqttOffline));
    applyMqttTimeoutFallback();
    logRuntimeEvent("Alarm: MQTT offline timeout");
    syncAlarmFromManager();
    gPendingAlarmStatusPublish = true;
    gDisplay.invalidateAll();
  }

  updateSafety();
  updateControl();
  gHeater.update();
  gRt.heatOn = gHeater.isOn();
  gAlarm.update();

  const ProcessStage* stage = gStages.getCurrentStage();
  const uint32_t remaining = gStages.getRemainingSeconds();

  if (gRt.runState == RunState::Complete) {
    gHeater.setOutputPercent(0.0f);
    gHeater.setEnabled(false);
    gRt.heatOn = false;
    if (!gCompletionHandled) {
      DBG_PRINTLN("Stage complete");
      gAlarm.notifyStageComplete();
      gRt.pendingProfileCompletePublish = true;
      logRuntimeEvent("Run complete");
      gCompletionHandled = true;
      gDisplay.invalidateAll();
    }
    if (gRt.mqttConnected && !debugMqttDisabledEffective()) gMqtt.publishProfileCompleteIfPending(gRt);
  } else {
    gCompletionHandled = false;
  }

  if (now - gLastStatusMs >= Config::STATUS_PUBLISH_MS) {
    gLastStatusMs = now;
    if (gRt.mqttConnected) {
      gMqtt.publishStatus(gRt, stage ? stage->name : "", remaining);
      if (!debugMqttDisabledEffective()) gMqtt.publishProfileCompleteIfPending(gRt);
    }
    gStorage.save(gCfg);
  }

  if (gPendingAlarmStatusPublish && gRt.mqttConnected) {
    gMqtt.publishStatus(gRt, stage ? stage->name : "", remaining);
    gPendingAlarmStatusPublish = false;
  }

  if (gRt.pendingEventLogPublish && gRt.mqttConnected) {
    gMqtt.publishEventLog(gRt);
    gRt.pendingEventLogPublish = false;
  }

  static uint32_t lastDebugStateMs = 0;
  if (gDebugEnabled && now - lastDebugStateMs >= 2000) {
    lastDebugStateMs = now;
    debugPrintState("loop");
  }

  gDisplay.draw(gCfg, gRt, stage, remaining);
  delay(1);
}
