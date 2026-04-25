#include <Arduino.h>
#include <M5Dial.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
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
#include "CommandRouter.h"
#include "IntegrationManager.h"
#include "DisplayManager.h"
#include "MenuSystem.h"
#include "DebugControl.h"
#include "TestingMode.h"
#include "platform/m5dial/M5DialBuzzer.h"
#include "platform/m5dial/M5DialDigitalOut.h"

bool gDebugEnabled = true;
bool gDebugDisableWifi = false;
bool gDebugDisableMqtt = false;
bool gDebugVerboseInput = false;
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
IntegrationBinding gBinding;
IntegrationManager gIntegration;
DisplayManager gDisplay;
MenuSystem gMenu;
MenuRenderState gMenuRenderState;
uint32_t gLastStatusMs = 0, gLastPidMs = 0, gLastMqttServiceMs = 0, gHeatEvalWindowStart = 0;
float gHeatEvalStartTemp = NAN;
bool gCompletionHandled = false;
bool gPendingAlarmStatusPublish = false;
bool gOtaInitialized = false;

static void persistAndPublishConfig();
static void applyLocalAuthorityOverride(bool enabled);
static void logRuntimeEvent(const char* text);
static void logAlarmHistory(const char* text);
static void playUiTone(uint16_t frequency, uint32_t durationMs);
static float clampSetpointToLimits(float requested);
void syncAlarmFromManager();

static bool testingModeActive() {
  return TestingMode::enabled(gCfg);
}

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
constexpr uint32_t kAutoTuneIntroScreenMs = 1800;
constexpr uint32_t kAutoTuneExitHoldMs = 5000;

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

static void markAutoTuneSetpointStep(float targetTempC) {
  if (isfinite(targetTempC)) {
    char text[48] {};
    snprintf(text, sizeof(text), "Autotune target %.1fC", targetTempC);
    logRuntimeEvent(text);
  }
  playUiTone(2600, 80);
}

static void applyTunings(float kp, float ki, float kd) {
  gPid.setTunings(kp, ki, kd);
  gPid.setReverseActing(gCfg.pidDirection == PidDirection::Reverse);
  gPid.reset();
  gHeater.setMaxOutputPercent(gCfg.maxOutputPercent);
  gRt.currentKp = kp;
  gRt.currentKi = ki;
  gRt.currentKd = kd;
}

static void failAutoTuneAndRevert() {
  gAutoTune.active = false;
  gRt.autoTunePhase = AutoTunePhase::Failed;
  gRt.runState = RunState::Idle;
  gRt.uiMode = UiMode::SetpointAdjust;
  gRt.currentSetpointC = clampSetpointToLimits(gCfg.localSetpointC);
  gHeater.setMaxOutputPercent(100.0f);
  applyTunings(gCfg.pidKp, gCfg.pidKi, gCfg.pidKd);
}

static bool startAutoTune() {
  if (!gRt.sensorHealthy || isnan(gRt.currentTempC)) {
    logRuntimeEvent("Autotune blocked: sensor unavailable");
    return false;
  }
  if (gRt.runState == RunState::Running || gRt.runState == RunState::Paused) {
    logRuntimeEvent("Autotune blocked: stop run first");
    return false;
  }
  if (gRt.runState == RunState::AutoTune) {
    logRuntimeEvent("Autotune already running");
    return false;
  }
  if (gRt.operatingMode == OperatingMode::Integrated && !gCfg.localAuthorityOverride) {
    applyLocalAuthorityOverride(true);
    persistAndPublishConfig();
    logRuntimeEvent("Local override enabled for autotune");
  }
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
  gRt.uiMode = UiMode::AutoTuneIntro;
  gRt.autoTuneUiStateAtMs = millis();
  gRt.currentSetpointC = gAutoTune.targetTempC;
  gRt.autoTuneRiseTimeSec = 0.0f;
  gRt.autoTuneOvershootC = 0.0f;
  gRt.autoTuneSettlingSec = 0.0f;
  gRt.autoTuneQualityScore = 0.0f;
  gHeater.setMaxOutputPercent(kAutoTuneHeaterMaxPct);
  markAutoTuneSetpointStep(gAutoTune.targetTempC);
  logRuntimeEvent("Autotune started");
  return true;
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
  gRt.uiMode = UiMode::AutoTuneComplete;
  gRt.autoTuneUiStateAtMs = millis();
  gRt.currentSetpointC = clampSetpointToLimits(gCfg.localSetpointC);
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

static void initDebugTransport() {
  if (!gDebugEnabled) return;
  debugBegin(115200);
  DBG_PRINTLN("[BOOT] debug transport ready (USB CDC primary)");
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
  DBG_PRINTF("[%s] op=%s int=%u run=%u ui=%u temp=%.2f probeA=%.2f probeB=%.2f sp=%.2f mins=%lu out=%.1f heat=%d wifi=%d mqtt=%d ctrl=%d fallback=%d timer=%d alarm=%u test=%d mode=%s ff=%d probes=%u\n",
             tag,
             gRt.operatingMode == OperatingMode::Integrated ? "integrated" : "standalone",
             static_cast<unsigned>(gRt.integrationState),
             (unsigned)gRt.runState,
             (unsigned)gRt.uiMode,
             gRt.currentTempC,
             gRt.probeATempC,
             gRt.probeBTempC,
             gRt.currentSetpointC,
             (unsigned long)gRt.activeStageMinutes,
             gRt.heaterOutputPct,
             gRt.heatOn,
             gRt.wifiConnected,
             gRt.mqttConnected,
             gRt.controllerConnected,
             gRt.integratedFallbackActive,
             gRt.stageTimerStarted,
             (unsigned)gRt.activeAlarm,
             gRt.testingModeActive,
             gRt.sensorMode,
             gRt.feedForwardEnabled,
             static_cast<unsigned>(gRt.probeCount));
}

static void setupOta() {
  if (gOtaInitialized) return;

  String host = String("env-ctrl-");
  uint32_t chipId = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFULL);
  char suffix[7] {};
  snprintf(suffix, sizeof(suffix), "%06lX", static_cast<unsigned long>(chipId));
  host += suffix;

  ArduinoOTA.setHostname(host.c_str());
  ArduinoOTA.onStart([]() { DBG_LOGLN("[OTA] Start"); });
  ArduinoOTA.onEnd([]() { DBG_LOGLN("[OTA] End"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    const unsigned int pct = total == 0 ? 0U : static_cast<unsigned int>((progress * 100U) / total);
    DBG_LOGF("[OTA] Progress: %u%%\n", pct);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    DBG_LOGF("[OTA] Error[%u]\n", static_cast<unsigned>(error));
  });
  ArduinoOTA.begin();
  gOtaInitialized = true;
  DBG_LOGF("[OTA] Ready hostname=%s ip=%s\n", host.c_str(), WiFi.localIP().toString().c_str());
}

static const char* alarmText(AlarmCode code) {
  switch (code) {
    case AlarmCode::SensorFault: return "SENSOR FAULT";
    case AlarmCode::OverTemp: return "OVER TEMP";
    case AlarmCode::HeatingIneffective: return "NO TEMP RISE";
    case AlarmCode::MqttOffline: return "MQTT OFFLINE";
    case AlarmCode::LowProcessTemp: return "LOW TEMP ALERT";
    case AlarmCode::HighProcessTemp: return "HIGH TEMP ALERT";
    default: return "OK";
  }
}

static bool isAlarmEnabled(AlarmCode code) {
  if (TestingMode::alarmsForcedOff(gCfg)) return false;
  switch (code) {
    case AlarmCode::SensorFault: return gCfg.alarmEnableSensorFault;
    case AlarmCode::OverTemp: return gCfg.alarmEnableOverTemp;
    case AlarmCode::HeatingIneffective: return gCfg.alarmEnableHeatingIneffective;
    case AlarmCode::MqttOffline: return gCfg.alarmEnableMqttOffline;
    case AlarmCode::LowProcessTemp: return gCfg.alarmEnableLowProcessTemp;
    case AlarmCode::HighProcessTemp: return gCfg.alarmEnableHighProcessTemp;
    case AlarmCode::None:
    default:
      return false;
  }
}

static void syncLegacyAlarmFlags() {
  gCfg.tempAlarmEnabled = gCfg.alarmEnableLowProcessTemp || gCfg.alarmEnableHighProcessTemp;
}

static bool allAlarmTogglesEnabled() {
  return gCfg.alarmEnableSensorFault &&
         gCfg.alarmEnableOverTemp &&
         gCfg.alarmEnableHeatingIneffective &&
         gCfg.alarmEnableMqttOffline &&
         gCfg.alarmEnableLowProcessTemp &&
         gCfg.alarmEnableHighProcessTemp;
}

static bool allAlarmTogglesDisabled() {
  return !gCfg.alarmEnableSensorFault &&
         !gCfg.alarmEnableOverTemp &&
         !gCfg.alarmEnableHeatingIneffective &&
         !gCfg.alarmEnableMqttOffline &&
         !gCfg.alarmEnableLowProcessTemp &&
         !gCfg.alarmEnableHighProcessTemp;
}

static void setAllAlarmToggles(bool enabled) {
  gCfg.alarmEnableSensorFault = enabled;
  gCfg.alarmEnableOverTemp = enabled;
  gCfg.alarmEnableHeatingIneffective = enabled;
  gCfg.alarmEnableMqttOffline = enabled;
  gCfg.alarmEnableLowProcessTemp = enabled;
  gCfg.alarmEnableHighProcessTemp = enabled;
  syncLegacyAlarmFlags();
}

static const char* alarmToggleText(bool enabled) {
  return enabled ? "On" : "Off [TEST]";
}

static uint8_t alarmHistoryMenuOffset(MenuItemId id) {
  return static_cast<uint8_t>(static_cast<uint16_t>(id) - static_cast<uint16_t>(MenuItemId::AlarmLogEntry0));
}

static bool tryGetAlarmToggle(MenuItemId id, bool*& flag) {
  flag = nullptr;
  switch (id) {
    case MenuItemId::AlarmSensorFault: flag = &gCfg.alarmEnableSensorFault; return true;
    case MenuItemId::AlarmOverTemp: flag = &gCfg.alarmEnableOverTemp; return true;
    case MenuItemId::AlarmHeatingIneffective: flag = &gCfg.alarmEnableHeatingIneffective; return true;
    case MenuItemId::AlarmMqttOffline: flag = &gCfg.alarmEnableMqttOffline; return true;
    case MenuItemId::AlarmLowProcessTemp: flag = &gCfg.alarmEnableLowProcessTemp; return true;
    case MenuItemId::AlarmHighProcessTemp: flag = &gCfg.alarmEnableHighProcessTemp; return true;
    default: return false;
  }
}

static void formatMsTimestamp(uint32_t atMs, char* out, size_t outSize) {
  const uint32_t totalSeconds = atMs / 1000UL;
  const uint32_t hours = totalSeconds / 3600UL;
  const uint32_t minutes = (totalSeconds / 60UL) % 60UL;
  const uint32_t seconds = totalSeconds % 60UL;
  snprintf(out, outSize, "%02lu:%02lu:%02lu",
           static_cast<unsigned long>(hours),
           static_cast<unsigned long>(minutes),
           static_cast<unsigned long>(seconds));
}

static void clearAlarmHistory() {
  memset(gRt.alarmHistory, 0, sizeof(gRt.alarmHistory));
  gRt.alarmHistoryHead = 0;
  gRt.alarmHistoryCount = 0;
}

static void logAlarmHistory(const char* text) {
  if (!text || text[0] == '\0') return;
  AlarmHistoryEntry& entry = gRt.alarmHistory[gRt.alarmHistoryHead];
  entry.atMs = millis();
  strlcpy(entry.text, text, sizeof(entry.text));
  gRt.alarmHistoryHead = (gRt.alarmHistoryHead + 1) % CoreConfig::ALARM_HISTORY_CAPACITY;
  if (gRt.alarmHistoryCount < CoreConfig::ALARM_HISTORY_CAPACITY) ++gRt.alarmHistoryCount;
}

static void raiseAlarm(AlarmCode code, const char* eventText, bool beep = true) {
  if (!isAlarmEnabled(code)) return;
  const bool changed = gAlarm.getAlarm() != code;
  gAlarm.setAlarm(code, alarmText(code), beep);
  if (changed) {
    logRuntimeEvent(eventText);
    logAlarmHistory(eventText);
  }
}

static float clampSetpointToLimits(float requested) {
  const float minLimit = min(gCfg.minSetpointC, gCfg.maxSetpointC);
  const float maxLimit = max(gCfg.minSetpointC, gCfg.maxSetpointC);
  return constrain(requested, minLimit, maxLimit);
}

static const char* controlAuthorityText(ControlAuthority authority) {
  switch (authority) {
    case ControlAuthority::Controller: return "CONTROLLER";
    case ControlAuthority::LocalOverride: return "LOCAL OVERRIDE";
    case ControlAuthority::Local:
    default:
      return "LOCAL";
  }
}

static void playUiTone(uint16_t frequency, uint32_t durationMs) {
  if (gCfg.buzzerEnabled) M5Dial.Speaker.tone(frequency, durationMs);
}

static void syncControlAuthority() {
  if (gRt.operatingMode == OperatingMode::Standalone) gRt.controlAuthority = ControlAuthority::Local;
  else if (gCfg.localAuthorityOverride) gRt.controlAuthority = ControlAuthority::LocalOverride;
  else gRt.controlAuthority = ControlAuthority::Controller;

  gRt.controlMode = (gRt.controlAuthority == ControlAuthority::Controller) ? ControlMode::Remote : ControlMode::Local;
}

static void applyTestingModeOperatingOverride() {
  if (!gRt.testingModeActive) return;

  // Keep testing mode on the integrated runtime path so the device can
  // connect to the automation controller and receive controller commands.
  gRt.operatingMode = OperatingMode::Integrated;
  if (gRt.integrationState == IntegrationState::None) {
    gRt.integrationState = IntegrationState::Enrolled;
  }
}

static bool localRuntimeAuthorityActive() {
  return gRt.controlAuthority != ControlAuthority::Controller;
}

static bool controllerRuntimeAuthorityActive() {
  return gRt.operatingMode == OperatingMode::Integrated && gRt.controlAuthority == ControlAuthority::Controller;
}

static bool localSetpointAdjustmentAllowed() {
  if (localRuntimeAuthorityActive()) return true;

  // When integrated but no controller-authored control profile exists on the
  // device yet, keep local setpoint entry available.
  return gRt.operatingMode == OperatingMode::Integrated &&
         gCfg.profileCount == 0 &&
         (gRt.runState == RunState::Idle || gRt.runState == RunState::Complete);
}

static void normalizePersistentConfig() {
  gCfg.minSetpointC = constrain(gCfg.minSetpointC, 0.0f, 140.0f);
  gCfg.maxSetpointC = constrain(gCfg.maxSetpointC, 0.0f, 140.0f);
  if (gCfg.minSetpointC > gCfg.maxSetpointC) {
    const float tmp = gCfg.minSetpointC;
    gCfg.minSetpointC = gCfg.maxSetpointC;
    gCfg.maxSetpointC = tmp;
  }

  gCfg.overTempC = max(gCfg.overTempC, gCfg.maxSetpointC);
  gCfg.localSetpointC = clampSetpointToLimits(gCfg.localSetpointC);
  gRt.currentSetpointC = clampSetpointToLimits(gRt.currentSetpointC);
  gRt.desiredSetpointC = clampSetpointToLimits(gRt.desiredSetpointC);
  gCfg.maxOutputPercent = constrain(gCfg.maxOutputPercent, 0.0f, 100.0f);

  gCfg.lowAlarmC = constrain(gCfg.lowAlarmC, -20.0f, 140.0f);
  gCfg.highAlarmC = constrain(gCfg.highAlarmC, -20.0f, 140.0f);
  if (gCfg.lowAlarmC > gCfg.highAlarmC) {
    const float tmp = gCfg.lowAlarmC;
    gCfg.lowAlarmC = gCfg.highAlarmC;
    gCfg.highAlarmC = tmp;
  }
  gCfg.alarmHysteresisC = constrain(gCfg.alarmHysteresisC, 0.1f, 10.0f);
  syncLegacyAlarmFlags();
}

static void applyPersistentRuntimeSettings() {
  normalizePersistentConfig();
  gPid.setReverseActing(gCfg.pidDirection == PidDirection::Reverse);
  gHeater.setMaxOutputPercent(gCfg.maxOutputPercent);
  gRt.controlEnabled = gCfg.controlEnabled;
  gRt.testingModeActive = testingModeActive();
  M5Dial.Display.setBrightness(gCfg.displayBrightness);
  applyTestingModeOperatingOverride();
  syncControlAuthority();
}

static void applyLocalAuthorityOverride(bool enabled) {
  if (gRt.operatingMode != OperatingMode::Integrated) {
    gCfg.localAuthorityOverride = false;
  } else {
    gCfg.localAuthorityOverride = enabled;
  }
  syncControlAuthority();
}

static void formatMenuItemValue(MenuItemId id, MenuRenderItem& item, void*) {
  item.value[0] = '\0';
  switch (id) {
    case MenuItemId::StatusOperatingMode:
      strlcpy(item.value, gRt.operatingMode == OperatingMode::Integrated ? "Integrated" : "Standalone", sizeof(item.value));
      break;
    case MenuItemId::StatusAuthority:
      strlcpy(item.value, controlAuthorityText(gRt.controlAuthority), sizeof(item.value));
      break;
    case MenuItemId::StatusSystemName:
    case MenuItemId::IntegrationSystemName:
      strlcpy(item.value, gRt.systemName[0] ? gRt.systemName : "Unbound", sizeof(item.value));
      break;
    case MenuItemId::StatusControllerLink:
    case MenuItemId::IntegrationControllerLink:
      strlcpy(item.value, gRt.controllerConnected ? "Connected" : "Offline", sizeof(item.value));
      break;
    case MenuItemId::StatusWifi:
    case MenuItemId::NetworkWifiStatus:
      strlcpy(item.value, gRt.wifiConnected ? WiFi.SSID().c_str() : "Disconnected", sizeof(item.value));
      break;
    case MenuItemId::StatusMqtt:
      strlcpy(item.value, gRt.mqttConnected ? "Connected" : "Disconnected", sizeof(item.value));
      break;
    case MenuItemId::StatusProcessVariable:
      snprintf(item.value, sizeof(item.value), "%.1f C", gRt.currentTempC);
      break;
    case MenuItemId::StatusOutput:
      snprintf(item.value, sizeof(item.value), "%.0f %%", gRt.heaterOutputPct);
      break;
    case MenuItemId::StatusAlarm:
      strlcpy(item.value, gRt.alarmText, sizeof(item.value));
      break;
    case MenuItemId::ControlLocalSetpoint:
      snprintf(item.value, sizeof(item.value), "%.1f C", gCfg.localSetpointC);
      break;
    case MenuItemId::ControlEnable:
      strlcpy(item.value, gCfg.controlEnabled ? "Enabled" : "Disabled", sizeof(item.value));
      break;
    case MenuItemId::ControlLocalOverride:
      strlcpy(item.value,
              gRt.operatingMode == OperatingMode::Integrated
                  ? (gCfg.localAuthorityOverride ? "On" : "Off")
                  : "N/A",
              sizeof(item.value));
      break;
    case MenuItemId::ControlMinLimit:
      snprintf(item.value, sizeof(item.value), "%.1f C", gCfg.minSetpointC);
      break;
    case MenuItemId::ControlMaxLimit:
      snprintf(item.value, sizeof(item.value), "%.1f C", gCfg.maxSetpointC);
      break;
    case MenuItemId::ControlLowAlarm:
      snprintf(item.value, sizeof(item.value), "%.1f C", gCfg.lowAlarmC);
      break;
    case MenuItemId::ControlHighAlarm:
      snprintf(item.value, sizeof(item.value), "%.1f C", gCfg.highAlarmC);
      break;
    case MenuItemId::AlarmAll:
      if (allAlarmTogglesEnabled()) strlcpy(item.value, "On", sizeof(item.value));
      else if (allAlarmTogglesDisabled()) strlcpy(item.value, "Off [TEST]", sizeof(item.value));
      else strlcpy(item.value, "Mixed", sizeof(item.value));
      break;
    case MenuItemId::AlarmSensorFault:
      strlcpy(item.value, alarmToggleText(gCfg.alarmEnableSensorFault), sizeof(item.value));
      break;
    case MenuItemId::AlarmOverTemp:
      strlcpy(item.value, alarmToggleText(gCfg.alarmEnableOverTemp), sizeof(item.value));
      break;
    case MenuItemId::AlarmHeatingIneffective:
      strlcpy(item.value, alarmToggleText(gCfg.alarmEnableHeatingIneffective), sizeof(item.value));
      break;
    case MenuItemId::AlarmMqttOffline:
      strlcpy(item.value, alarmToggleText(gCfg.alarmEnableMqttOffline), sizeof(item.value));
      break;
    case MenuItemId::AlarmLowProcessTemp:
      strlcpy(item.value, alarmToggleText(gCfg.alarmEnableLowProcessTemp), sizeof(item.value));
      break;
    case MenuItemId::AlarmHighProcessTemp:
      strlcpy(item.value, alarmToggleText(gCfg.alarmEnableHighProcessTemp), sizeof(item.value));
      break;
    case MenuItemId::AlarmLogEntry0:
    case MenuItemId::AlarmLogEntry1:
    case MenuItemId::AlarmLogEntry2:
    case MenuItemId::AlarmLogEntry3:
    case MenuItemId::AlarmLogEntry4:
    case MenuItemId::AlarmLogEntry5:
    case MenuItemId::AlarmLogEntry6:
    case MenuItemId::AlarmLogEntry7: {
      const uint8_t offset = alarmHistoryMenuOffset(id);
      if (offset < gRt.alarmHistoryCount) {
        const int newestIndex = static_cast<int>(gRt.alarmHistoryHead + CoreConfig::ALARM_HISTORY_CAPACITY - 1 - offset) %
                                CoreConfig::ALARM_HISTORY_CAPACITY;
        const AlarmHistoryEntry& entry = gRt.alarmHistory[newestIndex];
        strlcpy(item.label, entry.text[0] ? entry.text : "Alarm Entry", sizeof(item.label));
        formatMsTimestamp(entry.atMs, item.value, sizeof(item.value));
      } else {
        strlcpy(item.label, "--", sizeof(item.label));
      }
      break;
    }
    case MenuItemId::PidKp:
      snprintf(item.value, sizeof(item.value), "%.2f", gCfg.pidKp);
      break;
    case MenuItemId::PidKi:
      snprintf(item.value, sizeof(item.value), "%.3f", gCfg.pidKi);
      break;
    case MenuItemId::PidKd:
      snprintf(item.value, sizeof(item.value), "%.2f", gCfg.pidKd);
      break;
    case MenuItemId::PidDirection:
      strlcpy(item.value, gCfg.pidDirection == PidDirection::Reverse ? "Reverse" : "Direct", sizeof(item.value));
      break;
    case MenuItemId::PidOutputLimit:
      snprintf(item.value, sizeof(item.value), "%.0f %%", gCfg.maxOutputPercent);
      break;
    case MenuItemId::PidAutotune:
      if (gRt.runState == RunState::AutoTune) strlcpy(item.value, "Running", sizeof(item.value));
      else if (gRt.autoTunePhase == AutoTunePhase::PendingAccept) strlcpy(item.value, "Apply", sizeof(item.value));
      else if (gRt.autoTunePhase == AutoTunePhase::Failed) strlcpy(item.value, "Retry", sizeof(item.value));
      else strlcpy(item.value, "Start", sizeof(item.value));
      break;
    case MenuItemId::NetworkIpAddress:
      strlcpy(item.value, gRt.wifiConnected ? WiFi.localIP().toString().c_str() : "--", sizeof(item.value));
      break;
    case MenuItemId::NetworkBrokerHost:
      strlcpy(item.value, gBinding.brokerHost[0] ? gBinding.brokerHost : gCfg.mqttHost, sizeof(item.value));
      break;
    case MenuItemId::NetworkBrokerPort:
      snprintf(item.value, sizeof(item.value), "%u", gBinding.brokerPort ? gBinding.brokerPort : gCfg.mqttPort);
      break;
    case MenuItemId::IntegrationControllerId:
      strlcpy(item.value, gRt.controllerId[0] ? gRt.controllerId : "None", sizeof(item.value));
      break;
    case MenuItemId::DeviceBrightness:
      snprintf(item.value, sizeof(item.value), "%u", gCfg.displayBrightness);
      break;
    case MenuItemId::DeviceBuzzer:
      strlcpy(item.value, gCfg.buzzerEnabled ? "On" : "Off", sizeof(item.value));
      break;
    case MenuItemId::DeviceCalibrationOffset:
      snprintf(item.value, sizeof(item.value), "%.2f C", gCfg.tempOffsetC);
      break;
    case MenuItemId::DeviceDeviceId:
      strlcpy(item.value, gCfg.deviceId[0] ? gCfg.deviceId : "Unassigned", sizeof(item.value));
      break;
    case MenuItemId::DeviceFirmware:
      strlcpy(item.value, CoreConfig::FIRMWARE_VERSION, sizeof(item.value));
      break;
    default:
      break;
  }
}

static void rebuildMenuRenderState() {
  if (gRt.uiMode == UiMode::SettingsAdjust) gMenu.buildRenderState(gMenuRenderState, formatMenuItemValue, nullptr);
}

static void enterSettingsMode() {
  gMenu.reset();
  gRt.uiMode = UiMode::SettingsAdjust;
  rebuildMenuRenderState();
}

static void leaveSettingsMode() {
  gMenu.reset();
  if (gRt.runState == RunState::Running) gRt.uiMode = UiMode::Running;
  else if (gRt.runState == RunState::Paused) gRt.uiMode = UiMode::Paused;
  else gRt.uiMode = UiMode::SetpointAdjust;
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

static void persistAndPublishConfig() {
  applyPersistentRuntimeSettings();
  gStorage.save(gCfg);
  if (gRt.mqttConnected) gMqtt.publishConfig(gCfg, gRt);
}

static void adjustCurrentMenuValue(int32_t diff) {
  if (diff == 0) return;

  bool changed = false;
  switch (gMenu.currentItem().id) {
    case MenuItemId::ControlLocalSetpoint:
      gCfg.localSetpointC = clampSetpointToLimits(gCfg.localSetpointC + diff * 0.5f);
      gRt.currentSetpointC = gCfg.localSetpointC;
      changed = true;
      break;
    case MenuItemId::ControlEnable:
      gCfg.controlEnabled = diff > 0;
      changed = true;
      break;
    case MenuItemId::ControlLocalOverride:
      if (gRt.operatingMode == OperatingMode::Integrated) {
        applyLocalAuthorityOverride(diff > 0);
        changed = true;
      }
      break;
    case MenuItemId::ControlMinLimit:
      gCfg.minSetpointC = constrain(gCfg.minSetpointC + diff * 0.5f, 0.0f, 140.0f);
      changed = true;
      break;
    case MenuItemId::ControlMaxLimit:
      gCfg.maxSetpointC = constrain(gCfg.maxSetpointC + diff * 0.5f, 0.0f, 140.0f);
      changed = true;
      break;
    case MenuItemId::ControlLowAlarm:
      gCfg.lowAlarmC = constrain(gCfg.lowAlarmC + diff * 0.5f, -20.0f, 140.0f);
      changed = true;
      break;
    case MenuItemId::ControlHighAlarm:
      gCfg.highAlarmC = constrain(gCfg.highAlarmC + diff * 0.5f, -20.0f, 140.0f);
      changed = true;
      break;
    case MenuItemId::AlarmAll:
      setAllAlarmToggles(diff > 0);
      changed = true;
      break;
    case MenuItemId::AlarmSensorFault:
    case MenuItemId::AlarmOverTemp:
    case MenuItemId::AlarmHeatingIneffective:
    case MenuItemId::AlarmMqttOffline:
    case MenuItemId::AlarmLowProcessTemp:
    case MenuItemId::AlarmHighProcessTemp: {
      bool* flag = nullptr;
      if (tryGetAlarmToggle(gMenu.currentItem().id, flag) && flag != nullptr) {
        *flag = diff > 0;
        syncLegacyAlarmFlags();
        changed = true;
      }
      break;
    }
    case MenuItemId::PidKp:
      gCfg.pidKp = constrain(gCfg.pidKp + diff * 0.2f, 0.1f, 60.0f);
      changed = true;
      break;
    case MenuItemId::PidKi:
      gCfg.pidKi = constrain(gCfg.pidKi + diff * 0.01f, 0.001f, 2.0f);
      changed = true;
      break;
    case MenuItemId::PidKd:
      gCfg.pidKd = constrain(gCfg.pidKd + diff * 0.2f, 0.1f, 80.0f);
      changed = true;
      break;
    case MenuItemId::PidDirection:
      gCfg.pidDirection = (diff > 0) ? PidDirection::Reverse : PidDirection::Direct;
      changed = true;
      break;
    case MenuItemId::PidOutputLimit:
      gCfg.maxOutputPercent = constrain(gCfg.maxOutputPercent + diff * 5.0f, 0.0f, 100.0f);
      changed = true;
      break;
    case MenuItemId::DeviceBrightness:
      gCfg.displayBrightness = static_cast<uint8_t>(constrain(static_cast<int>(gCfg.displayBrightness) + static_cast<int>(diff * 8), 8, 255));
      changed = true;
      break;
    case MenuItemId::DeviceBuzzer:
      gCfg.buzzerEnabled = diff > 0;
      changed = true;
      break;
    case MenuItemId::DeviceCalibrationOffset:
      gCfg.tempOffsetC = constrain(gCfg.tempOffsetC + diff * 0.1f, -10.0f, 10.0f);
      gTempSensor.setCalibrationOffset(gCfg.tempOffsetC);
      changed = true;
      break;
    default:
      break;
  }

  if (!changed) return;

  if (gRt.activeAlarm != AlarmCode::None && !isAlarmEnabled(gRt.activeAlarm)) {
    gAlarm.clearAlarm(AlarmControlSource::System);
    syncAlarmFromManager();
  }

  applyTunings(gCfg.pidKp, gCfg.pidKi, gCfg.pidKd);
  persistAndPublishConfig();
  rebuildMenuRenderState();
}

static void triggerMenuAction(MenuItemId id) {
  switch (id) {
    case MenuItemId::ControlStopRun:
      gStages.stop();
      gCompletionHandled = false;
      logRuntimeEvent("Run stopped (menu)");
      break;
    case MenuItemId::PidAutotune:
      if (gRt.autoTunePhase == AutoTunePhase::PendingAccept) {
        gRt.previousKp = gCfg.pidKp;
        gRt.previousKi = gCfg.pidKi;
        gRt.previousKd = gCfg.pidKd;
        persistActivePidAndQuality();
        gRt.autoTunePhase = AutoTunePhase::Complete;
        gRt.uiMode = UiMode::SetpointAdjust;
        logRuntimeEvent("Autotune accepted (menu)");
        if (gRt.mqttConnected) gMqtt.publishConfig(gCfg, gRt);
      } else {
        startAutoTune();
      }
      break;
    case MenuItemId::NetworkClearWifi:
    case MenuItemId::DeviceResetWifi:
      gWifi.resetSettings();
      logRuntimeEvent("Wi-Fi reset (menu)");
      break;
    case MenuItemId::IntegrationUnpair:
      gIntegration.unpairToStandalone(true);
      gBinding = gIntegration.binding();
      applyLocalAuthorityOverride(false);
      logRuntimeEvent("Device unpaired");
      leaveSettingsMode();
      break;
    case MenuItemId::AlarmLogClearAll:
      clearAlarmHistory();
      logRuntimeEvent("Alarm log cleared");
      break;
    default:
      break;
  }
}

static void activateCurrentMenuSelection() {
  const MenuItemDefinition& item = gMenu.currentItem();

  if (gMenu.isEditing()) {
    gMenu.setEditing(false);
    rebuildMenuRenderState();
    return;
  }

  switch (item.kind) {
    case MenuItemKind::Submenu:
      gMenu.enter(item.target);
      break;
    case MenuItemKind::Back:
      if (!gMenu.back()) leaveSettingsMode();
      break;
    case MenuItemKind::Exit:
      leaveSettingsMode();
      break;
    case MenuItemKind::Action:
      triggerMenuAction(item.id);
      break;
    case MenuItemKind::Value:
      gMenu.setEditing(true);
      break;
    case MenuItemKind::ReadOnly:
    default:
      playUiTone(1800, 25);
      break;
  }

  rebuildMenuRenderState();
}

static void debugPrintBootNetworkTargets() {
  String ssidStr = WiFi.SSID();
  const char* ssid = (ssidStr.length() > 0) ? ssidStr.c_str() : "<not-associated-yet>";
  const char* mqttHost = TestingMode::mqttHost(gCfg);
  const uint16_t mqttPort = TestingMode::mqttPort(gCfg);

  IPAddress mqttIp;
  bool resolved = false;
  if (mqttHost[0] != '\0') {
    resolved = WiFi.hostByName(mqttHost, mqttIp);
  }

  DBG_PRINTF("Boot network target mode=%s SSID=%s test=%d\n",
             gRt.operatingMode == OperatingMode::Integrated ? "integrated" : "standalone",
             ssid,
             gRt.testingModeActive);
  if (resolved) {
    DBG_PRINTF("Boot MQTT target host=%s resolved_ip=%s port=%u tls=%d\n",
               mqttHost,
               mqttIp.toString().c_str(),
               mqttPort,
               TestingMode::mqttTlsEnabled(gCfg));
  } else {
    DBG_PRINTF("Boot MQTT target host=%s resolved_ip=<unresolved> port=%u tls=%d\n",
               mqttHost,
               mqttPort,
               TestingMode::mqttTlsEnabled(gCfg));
  }
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
  const char* safePayload = payload ? payload : "";
  DBG_PRINTF("MQTT cmd topic=%s payloadBytes=%u\n", topic ? topic : "", static_cast<unsigned>(strlen(safePayload)));
  if (gIntegration.handleMqttMessage(topic, payload)) {
    return;
  }

  CommandRouterServices services {
    gCfg,
    gRt,
    gStorage,
    gStages,
    gMqtt,
    gDisplay,
    gAlarm,
    gTempSensor,
    gWifi,
    gCompletionHandled,
    candidateWithinGuardrails,
    applyTunings,
    startAutoTune,
    persistActivePidAndQuality,
    logRuntimeEvent,
    syncAlarmFromManager,
    upsertProfileFromJson
  };
  routeMqttCommand(topic, payload, services);
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
    playUiTone(3200, 20);
  }
}

void handleButton() {
  if (M5Dial.BtnA.wasClicked()) {
    DBG_PRINTLN("Button clicked");
    if (gRt.uiMode == UiMode::AutoTuneComplete || gRt.autoTunePhase == AutoTunePhase::PendingAccept) {
      gRt.previousKp = gCfg.pidKp;
      gRt.previousKi = gCfg.pidKi;
      gRt.previousKd = gCfg.pidKd;
      persistActivePidAndQuality();
      gRt.autoTunePhase = AutoTunePhase::Complete;
      gRt.uiMode = UiMode::SetpointAdjust;
      logRuntimeEvent("Autotune accepted (local)");
      if (gRt.mqttConnected) gMqtt.publishConfig(gCfg, gRt);
      gDisplay.invalidateAll();
      return;
    }
    if (gRt.runState == RunState::AutoTune || gRt.uiMode == UiMode::AutoTuneIntro || gRt.uiMode == UiMode::AutoTuneActive) {
      playUiTone(1800, 25);
      return;
    }
    if (gRt.uiMode == UiMode::SettingsAdjust) {
      activateCurrentMenuSelection();
      gDisplay.invalidateAll();
      return;
    }

    if (!localRuntimeAuthorityActive()) {
      logRuntimeEvent("Local runtime control blocked");
      playUiTone(1800, 40);
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
    if (gRt.runState == RunState::AutoTune || gRt.uiMode == UiMode::AutoTuneIntro || gRt.uiMode == UiMode::AutoTuneActive) {
      return;
    }
    if (gRt.autoTunePhase == AutoTunePhase::PendingAccept) {
      applyTunings(gCfg.pidKp, gCfg.pidKi, gCfg.pidKd);
      gRt.autoTunePhase = AutoTunePhase::Failed;
      gRt.uiMode = UiMode::SetpointAdjust;
      gRt.autoTuneQualityScore = 0.0f;
      logRuntimeEvent("Autotune rejected (local)");
      gDisplay.invalidateAll();
      playUiTone(2200, 80);
      return;
    }

    if (gRt.uiMode != UiMode::SettingsAdjust) {
      enterSettingsMode();
      logRuntimeEvent("Settings opened");
      gDisplay.invalidateAll();
      playUiTone(2200, 80);
    }
  }
}

void processInput() {
  M5Dial.update();
  handleTouch();

  if ((gRt.runState == RunState::AutoTune || gRt.uiMode == UiMode::AutoTuneIntro || gRt.uiMode == UiMode::AutoTuneActive) &&
      M5Dial.BtnA.pressedFor(kAutoTuneExitHoldMs)) {
    failAutoTuneAndRevert();
    logRuntimeEvent("Autotune canceled by hold");
    gDisplay.invalidateAll();
    playUiTone(1800, 120);
    return;
  }

  if (gRt.activeAlarm != AlarmCode::None && gDisplay.wasAlarmPillTouched()) {
    DBG_PRINTLN("Display alarm-pill reset");
    gAlarm.clearAlarm(AlarmControlSource::LocalUi);
    syncAlarmFromManager();
    gDisplay.invalidateAll();
    playUiTone(3200, 20);
  }

  handleButton();

  static int32_t lastEnc = 0;
  const int32_t enc = M5Dial.Encoder.read();
  const int32_t diff = enc - lastEnc;
  if (diff == 0) return;
  lastEnc = enc;

  if (gRt.uiMode == UiMode::SetpointAdjust && localSetpointAdjustmentAllowed()) {
    gRt.controlMode = ControlMode::Local;
    gCfg.localSetpointC = clampSetpointToLimits(gCfg.localSetpointC + diff * 0.5f);
    gRt.currentSetpointC = gCfg.localSetpointC;
    if (gDebugVerboseInput) {
      DBG_PRINTF("Encoder setpoint diff=%ld localSetpoint=%.1f\n", (long)diff, gCfg.localSetpointC);
    }
  } else if (gRt.uiMode == UiMode::StageTimeAdjust && localRuntimeAuthorityActive()) {
    int32_t mins = static_cast<int32_t>(gCfg.manualStageMinutes) + diff;
    if (mins < 0) mins = 0;
    if (mins > 480) mins = 480;
    gCfg.manualStageMinutes = static_cast<uint32_t>(mins);
    gRt.activeStageMinutes = gCfg.manualStageMinutes;
    if (gDebugVerboseInput) {
      DBG_PRINTF("Encoder minutes diff=%ld minutes=%lu\n", (long)diff, (unsigned long)gCfg.manualStageMinutes);
    }
  } else if (gRt.uiMode == UiMode::SettingsAdjust) {
    if (gMenu.isEditing()) adjustCurrentMenuValue(diff);
    else if (gMenu.navigate(diff)) rebuildMenuRenderState();
  } else if (gDebugVerboseInput) {
    DBG_PRINTF("Encoder diff=%ld ignored ui=%u\n", (long)diff, (unsigned)gRt.uiMode);
  }

  gDisplay.requestImmediateUi();
}

static void updateTemperatureAlertState() {
  gRt.lowTempAlarmActive = false;
  gRt.highTempAlarmActive = false;

  if (!gRt.sensorHealthy || isnan(gRt.currentTempC)) return;

  if (gCfg.alarmEnableLowProcessTemp && gRt.currentTempC <= gCfg.lowAlarmC) {
    gRt.lowTempAlarmActive = true;
  }
  if (gCfg.alarmEnableHighProcessTemp && gRt.currentTempC >= gCfg.highAlarmC) {
    gRt.highTempAlarmActive = true;
  }
}

void updateSafety() {
  gRt.sensorHealthy = gTempSensor.isHealthy();
  if (!gRt.sensorHealthy) {
    DBG_PRINTLN("Alarm: Sensor fault");
    raiseAlarm(AlarmCode::SensorFault, "Alarm: Sensor fault");
    gRt.runState = RunState::Fault;
  } else if (gRt.currentTempC >= gCfg.overTempC) {
    DBG_PRINTLN("Alarm: Over temp");
    raiseAlarm(AlarmCode::OverTemp, "Alarm: Over temp");
    gRt.runState = RunState::Fault;
  } else {
    updateTemperatureAlertState();
    if (gRt.lowTempAlarmActive) {
      raiseAlarm(AlarmCode::LowProcessTemp, "Alarm: Low process temp", false);
    } else if (gRt.highTempAlarmActive) {
      raiseAlarm(AlarmCode::HighProcessTemp, "Alarm: High process temp", false);
    } else if ((gAlarm.getAlarm() == AlarmCode::LowProcessTemp || gAlarm.getAlarm() == AlarmCode::HighProcessTemp) &&
               gRt.currentTempC > (gCfg.lowAlarmC + gCfg.alarmHysteresisC) &&
               gRt.currentTempC < (gCfg.highAlarmC - gCfg.alarmHysteresisC)) {
      gAlarm.clearAlarm(AlarmControlSource::System);
    }

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
        raiseAlarm(AlarmCode::HeatingIneffective, "Alarm: Heating ineffective");
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

  if (!gRt.sensorHealthy || gRt.runState == RunState::Fault || !gCfg.controlEnabled) {
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

  if (gRt.runState == RunState::Idle ||
      gRt.runState == RunState::Complete) {
    gRt.currentSetpointC = clampSetpointToLimits(gCfg.localSetpointC);
  }

  gRt.activeStageMinutes = gCfg.manualStageMinutes;

  if (gRt.runState == RunState::Running) {
    gStages.update(gRt.currentTempC);
  }

  if (gRt.runState == RunState::Paused || gRt.runState == RunState::Complete ||
      gRt.uiMode == UiMode::StageTimeAdjust) {
    gRt.heatingEnabled = false;
    gRt.heaterOutputPct = 0.0f;
    gPid.reset();
    gHeater.setEnabled(false);
    gHeater.setOutputPercent(0.0f);
    return;
  }

  gRt.heatingEnabled = true;
  gRt.currentSetpointC = clampSetpointToLimits(gRt.currentSetpointC);
  float pct = gPid.compute(gRt.currentSetpointC, gRt.currentTempC, dt);
  if (!isnan(gRt.currentTempC) &&
      gCfg.pidDirection == PidDirection::Direct &&
      gRt.currentTempC >= gRt.currentSetpointC) {
    pct = 0.0f;
  }
  gRt.heaterOutputPct = pct;
  gHeater.setEnabled(true);
  gHeater.setOutputPercent(gRt.heaterOutputPct);
}

void setup() {
  auto cfg = M5.config();
  M5Dial.begin(cfg, true, false);
  M5Dial.Display.setRotation(0);
  initDebugTransport();
  delay(50);
  debugLogBanner();

  gStorage.begin();
  gStorage.load(gCfg);
  gStorage.loadIntegrationBinding(gBinding);
  gRt.currentSetpointC = gCfg.localSetpointC;
  gRt.activeStageMinutes = gCfg.manualStageMinutes;
  gRt.desiredSetpointC = gCfg.localSetpointC;
  gRt.desiredMinutes = gCfg.manualStageMinutes;
  strlcpy(gRt.desiredRunAction, "stop", sizeof(gRt.desiredRunAction));
  gRt.controlMode = (gCfg.controlLock == ControlLock::RemoteOnly) ? ControlMode::Remote : ControlMode::Local;
  gRt.uiMode = UiMode::SetpointAdjust;
  gRt.lastValidMqttConnectionAtMs = millis();
  gRt.lastAcceptedRemoteCommandAtMs = gRt.lastValidMqttConnectionAtMs;
  gIntegration.begin(&gBinding, &gCfg, &gRt, &gStorage, &gWifi, &gMqtt);
  Serial.printf("[BOOT] paired_metadata=%d operating_mode=%s integration_state=%u system_id=%s controller_id=%s\n",
                gRt.pairedMetadataPresent,
                gRt.operatingMode == OperatingMode::Integrated ? "integrated" : "standalone",
                static_cast<unsigned>(gRt.integrationState),
                gRt.systemId,
                gRt.controllerId);

  gTempSensor.begin();
  gTempSensor.setCalibrationOffset(gCfg.tempOffsetC);
  gTempSensor.setSmoothingFactor(gCfg.tempSmoothingAlpha);
  gTempSensor.setPlausibilityLimit(Config::DEFAULT_TEMP_MAX_RATE_C_PER_SEC);
  logRuntimeEvent(gTempSensor.isDualProbeMode() ? "Sensor mode: dual probe" : "Sensor mode: single probe");
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
  gAlarm.setSignalHandler([](bool on) {
    if (gCfg.buzzerEnabled) gBuzzer.set(on);
    else gBuzzer.set(false);
  });
  gAlarm.setCompletionHandler([]() {
    if (gCfg.buzzerEnabled) gBuzzer.playCompletionPattern();
  });
  gAlarm.begin();
  gAlarm.setLocalUiAlarmControlEnabled(true);
  gStages.begin(&gCfg, &gRt);
  applyPersistentRuntimeSettings();
  Serial.printf("[TEST] enabled=%d ssid=%s mqtt=%s:%u tls=%d alarms_forced_off=%d\n",
                gRt.testingModeActive,
                TestingMode::wifiSsid(),
                TestingMode::mqttHost(gCfg),
                TestingMode::mqttPort(gCfg),
                TestingMode::mqttTlsEnabled(gCfg),
                TestingMode::alarmsForcedOff(gCfg));

  if (!debugWifiDisabledEffective()) {
    if (gRt.testingModeActive) {
      Serial.printf("[BOOT] testing mode forcing network ssid=%s broker=%s:%u\n",
                    TestingMode::wifiSsid(),
                    TestingMode::mqttHost(gCfg),
                    TestingMode::mqttPort(gCfg));
      gWifi.beginIntegrated(TestingMode::wifiSsid(), TestingMode::wifiPassword());
    } else if (gIntegration.shouldBootIntegratedNetworking()) {
      Serial.printf("[BOOT] starting integrated network ssid=%s broker=%s:%u\n",
                    gIntegration.bootSsid(),
                    gIntegration.bootBrokerHost(),
                    gIntegration.bootBrokerPort());
      gWifi.beginIntegrated(gIntegration.bootSsid(), gIntegration.bootPassword());
    } else {
      Serial.printf("[BOOT] starting standalone network portal_timeout=%u\n",
                    static_cast<unsigned>(gCfg.wifiPortalTimeoutSec));
      gWifi.beginStandalone(gCfg.wifiPortalTimeoutSec);
    }
    debugPrintBootNetworkTargets();
    DBG_PRINTF("WiFi startup queued mqttHost=%s mqttPort=%u timeout=%u\n",
               TestingMode::mqttHost(gCfg),
               TestingMode::mqttPort(gCfg),
               static_cast<unsigned>(gCfg.wifiPortalTimeoutSec));
    DBG_PRINTF("WiFi commissioning AP: %s (pass=%s)\n",
               gWifi.getPortalApName(),
               maskSecret(gWifi.getPortalApPassword()).c_str());
    if (gWifi.isConnected()) setupOta();
  } else {
    DBG_LOGLN("WiFi disabled by debug/compile-time network mode");
    gRt.wifiConnected = false;
  }

  if (!debugMqttDisabledEffective()) {
    gMqtt.begin(&gCfg, &gRt);
    gMqtt.setCommandCallback(handleCommands);
    Serial.printf("[MQTT] startup target host=%s port=%u tls=%d auth=%d clientId=%s literal_ip=%d\n",
                  TestingMode::mqttHost(gCfg),
                  TestingMode::mqttPort(gCfg),
                  TestingMode::mqttTlsEnabled(gCfg),
                  TestingMode::mqttUseAuth(gCfg),
                  gMqtt.clientId(),
                  gRt.testingModeActive);
  } else {
    DBG_LOGLN("MQTT disabled by debug/compile-time network mode");
    gRt.mqttConnected = false;
  }

  gDisplay.begin();
  gDisplay.invalidateAll();
  logRuntimeEvent("System booted");
  debugPrintState("setup");
}

void loop() {
  processInput();
  const uint32_t now = millis();

  if (!debugWifiDisabledEffective()) {
    gWifi.update();
    gRt.wifiConnected = gWifi.isConnected();
    if (gRt.wifiConnected) {
      if (!gOtaInitialized) setupOta();
      ArduinoOTA.handle();
    }
  } else {
    gRt.wifiConnected = false;
  }

  gTempSensor.update();
  gRt.probeCount = gTempSensor.getProbeCount();
  strlcpy(gRt.sensorMode, gTempSensor.isDualProbeMode() ? "dual" : "single", sizeof(gRt.sensorMode));
  gRt.probeARawTempC = gTempSensor.getProbeARawCelsius();
  gRt.probeATempC = gTempSensor.getProbeACelsius();
  gRt.probeBRawTempC = gTempSensor.getProbeBRawCelsius();
  gRt.probeBTempC = gTempSensor.getProbeBCelsius();
  gRt.currentRawTempC = gRt.probeARawTempC;
  gRt.currentTempC = gRt.probeATempC;
  gRt.probeAHealthy = gTempSensor.isProbeAHealthy();
  gRt.probeBHealthy = gTempSensor.isProbeBHealthy();
  gRt.sensorHealthy = gRt.probeAHealthy;
  gRt.feedForwardEnabled = gRt.probeBHealthy;
  gRt.probeAPlausible = gTempSensor.isProbeAPlausible();
  gRt.probeBPlausible = gTempSensor.isProbeBPlausible();
  gRt.tempPlausible = gRt.probeAPlausible;
  static bool lastProbeAHealthy = false;
  static bool lastProbeBHealthy = false;
  static uint8_t lastProbeCount = 0xFF;
  if (lastProbeCount != gRt.probeCount) {
    char probeCountText[40] {};
    snprintf(probeCountText, sizeof(probeCountText), "Probes detected: %u", static_cast<unsigned>(gRt.probeCount));
    logRuntimeEvent(probeCountText);
    lastProbeCount = gRt.probeCount;
  }
  if (lastProbeAHealthy && !gRt.probeAHealthy) {
    logRuntimeEvent("Critical: Probe A fault");
  } else if (!lastProbeAHealthy && gRt.probeAHealthy) {
    logRuntimeEvent("Probe A healthy");
  }
  if (lastProbeBHealthy && !gRt.probeBHealthy) {
    logRuntimeEvent("Warning: Probe B unavailable");
  } else if (!lastProbeBHealthy && gRt.probeBHealthy) {
    logRuntimeEvent("Probe B healthy");
  }
  lastProbeAHealthy = gRt.probeAHealthy;
  lastProbeBHealthy = gRt.probeBHealthy;
  if (gTempSensor.hasNewValue()) {
    DBG_PRINTF("Temp update probeA=%.2fC probeB=%.2fC mode=%s ff=%d\n",
               gRt.probeATempC,
               gRt.probeBTempC,
               gRt.sensorMode,
               gRt.feedForwardEnabled);
  }

  if (!debugMqttDisabledEffective() && now - gLastMqttServiceMs >= 50) {
    gLastMqttServiceMs = now;
    if (gRt.wifiConnected) gMqtt.update();
  } else if (debugMqttDisabledEffective()) {
    gRt.mqttConnected = false;
  }

  gIntegration.update();
  applyTestingModeOperatingOverride();
  syncControlAuthority();

  if (gRt.uiMode == UiMode::AutoTuneIntro &&
      millis() - gRt.autoTuneUiStateAtMs >= kAutoTuneIntroScreenMs) {
    gRt.uiMode = UiMode::AutoTuneActive;
    gDisplay.invalidateAll();
  }

  if (remoteCommsTimedOut(now) && gAlarm.getAlarm() != AlarmCode::MqttOffline) {
    if (isAlarmEnabled(AlarmCode::MqttOffline)) {
      raiseAlarm(AlarmCode::MqttOffline, "Alarm: MQTT offline timeout");
      applyMqttTimeoutFallback();
      syncAlarmFromManager();
      gPendingAlarmStatusPublish = true;
      gDisplay.invalidateAll();
    }
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

  gBuzzer.update();

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

  if (gRt.uiMode == UiMode::SettingsAdjust) rebuildMenuRenderState();
  gDisplay.draw(gCfg, gRt, stage, remaining, gRt.uiMode == UiMode::SettingsAdjust ? &gMenuRenderState : nullptr);
  delay(1);
}
