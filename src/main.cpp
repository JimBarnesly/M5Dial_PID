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
IntegrationBinding gBinding;
IntegrationManager gIntegration;
DisplayManager gDisplay;
uint32_t gLastStatusMs = 0, gLastPidMs = 0, gLastMqttServiceMs = 0, gHeatEvalWindowStart = 0;
float gHeatEvalStartTemp = NAN;
bool gCompletionHandled = false;
bool gPendingAlarmStatusPublish = false;
bool gOtaInitialized = false;

static void persistAndPublishConfig();
static void applyLocalAuthorityOverride(bool enabled);
static void logRuntimeEvent(const char* text);

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
  gHeater.setMaxOutputPercent(100.0f);
  applyTunings(gCfg.pidKp, gCfg.pidKi, gCfg.pidKd);
}

static void startAutoTune() {
  if (!gRt.sensorHealthy || isnan(gRt.currentTempC)) return;
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
  DBG_PRINTF("[%s] op=%s int=%u run=%u ui=%u temp=%.2f probeA=%.2f probeB=%.2f sp=%.2f mins=%lu out=%.1f heat=%d wifi=%d mqtt=%d ctrl=%d fallback=%d timer=%d alarm=%u mode=%s ff=%d probes=%u\n",
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

static const char* settingsSectionText(SettingsSection section) {
  switch (section) {
    case SettingsSection::Status: return "STATUS";
    case SettingsSection::Control: return "CONTROL";
    case SettingsSection::Pid: return "PID SETTINGS";
    case SettingsSection::Network: return "NETWORK";
    case SettingsSection::Integration: return "INTEGRATION";
    case SettingsSection::Device: return "DEVICE";
    case SettingsSection::Exit: return "EXIT";
    default: return "SETTINGS";
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

static bool localRuntimeAuthorityActive() {
  return gRt.controlAuthority != ControlAuthority::Controller;
}

static bool controllerRuntimeAuthorityActive() {
  return gRt.operatingMode == OperatingMode::Integrated && gRt.controlAuthority == ControlAuthority::Controller;
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
}

static void applyPersistentRuntimeSettings() {
  normalizePersistentConfig();
  gPid.setReverseActing(gCfg.pidDirection == PidDirection::Reverse);
  gHeater.setMaxOutputPercent(gCfg.maxOutputPercent);
  gRt.controlEnabled = gCfg.controlEnabled;
  M5Dial.Display.setBrightness(gCfg.displayBrightness);
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

static uint8_t settingsItemCount(SettingsSection section) {
  switch (section) {
    case SettingsSection::Status: return 10;
    case SettingsSection::Control: return 10;
    case SettingsSection::Pid: return 7;
    case SettingsSection::Network: return 6;
    case SettingsSection::Integration: return 5;
    case SettingsSection::Device: return 7;
    case SettingsSection::Exit: return 1;
    default: return 1;
  }
}

static void refreshSettingsUiText() {
  if (gRt.uiMode != UiMode::SettingsAdjust) {
    gRt.settingsLabel[0] = '\0';
    gRt.settingsValue[0] = '\0';
    return;
  }

  if (gRt.settingsMenuLevel == SettingsMenuLevel::SectionList) {
    strlcpy(gRt.settingsLabel, "SECTION", sizeof(gRt.settingsLabel));
    strlcpy(gRt.settingsValue, settingsSectionText(gRt.settingsSection), sizeof(gRt.settingsValue));
    return;
  }

  const uint8_t item = gRt.settingsItemIndex;
  switch (gRt.settingsSection) {
    case SettingsSection::Status:
      switch (item) {
        case 0:
          strlcpy(gRt.settingsLabel, "OPERATING MODE", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, gRt.operatingMode == OperatingMode::Integrated ? "INTEGRATED" : "STANDALONE", sizeof(gRt.settingsValue));
          break;
        case 1:
          strlcpy(gRt.settingsLabel, "AUTHORITY", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, controlAuthorityText(gRt.controlAuthority), sizeof(gRt.settingsValue));
          break;
        case 2:
          strlcpy(gRt.settingsLabel, "SYSTEM NAME", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, gRt.systemName[0] ? gRt.systemName : "UNBOUND", sizeof(gRt.settingsValue));
          break;
        case 3:
          strlcpy(gRt.settingsLabel, "CONTROLLER", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, gRt.controllerConnected ? "CONNECTED" : "OFFLINE", sizeof(gRt.settingsValue));
          break;
        case 4:
          strlcpy(gRt.settingsLabel, "WI-FI", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, gRt.wifiConnected ? "CONNECTED" : "DISCONNECTED", sizeof(gRt.settingsValue));
          break;
        case 5:
          strlcpy(gRt.settingsLabel, "MQTT", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, gRt.mqttConnected ? "CONNECTED" : "DISCONNECTED", sizeof(gRt.settingsValue));
          break;
        case 6:
          strlcpy(gRt.settingsLabel, "PROCESS VAR", sizeof(gRt.settingsLabel));
          snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%.1f C", gRt.currentTempC);
          break;
        case 7:
          strlcpy(gRt.settingsLabel, "OUTPUT", sizeof(gRt.settingsLabel));
          snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%.0f %%", gRt.heaterOutputPct);
          break;
        case 8:
          strlcpy(gRt.settingsLabel, "ALARM", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, gRt.alarmText, sizeof(gRt.settingsValue));
          break;
        default:
          strlcpy(gRt.settingsLabel, "BACK", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, "SELECT", sizeof(gRt.settingsValue));
          break;
      }
      break;

    case SettingsSection::Control:
      switch (item) {
        case 0:
          strlcpy(gRt.settingsLabel, "LOCAL SETPOINT", sizeof(gRt.settingsLabel));
          snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%.1f C", gCfg.localSetpointC);
          break;
        case 1:
          strlcpy(gRt.settingsLabel, "CONTROL ENABLE", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, gCfg.controlEnabled ? "ENABLED" : "DISABLED", sizeof(gRt.settingsValue));
          break;
        case 2:
          strlcpy(gRt.settingsLabel, "TEMP STANDALONE", sizeof(gRt.settingsLabel));
          if (gRt.operatingMode == OperatingMode::Integrated) strlcpy(gRt.settingsValue, gCfg.localAuthorityOverride ? "ON" : "OFF", sizeof(gRt.settingsValue));
          else strlcpy(gRt.settingsValue, "N/A", sizeof(gRt.settingsValue));
          break;
        case 3:
          strlcpy(gRt.settingsLabel, "MIN TEMP LIMIT", sizeof(gRt.settingsLabel));
          snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%.1f C", gCfg.minSetpointC);
          break;
        case 4:
          strlcpy(gRt.settingsLabel, "MAX TEMP LIMIT", sizeof(gRt.settingsLabel));
          snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%.1f C", gCfg.maxSetpointC);
          break;
        case 5:
          strlcpy(gRt.settingsLabel, "LOW ALARM", sizeof(gRt.settingsLabel));
          snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%.1f C", gCfg.lowAlarmC);
          break;
        case 6:
          strlcpy(gRt.settingsLabel, "HIGH ALARM", sizeof(gRt.settingsLabel));
          snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%.1f C", gCfg.highAlarmC);
          break;
        case 7:
          strlcpy(gRt.settingsLabel, "ALARMS ENABLED", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, gCfg.tempAlarmEnabled ? "ON" : "OFF", sizeof(gRt.settingsValue));
          break;
        case 8:
          strlcpy(gRt.settingsLabel, "STOP RUN", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, "SELECT", sizeof(gRt.settingsValue));
          break;
        default:
          strlcpy(gRt.settingsLabel, "BACK", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, "SELECT", sizeof(gRt.settingsValue));
          break;
      }
      break;

    case SettingsSection::Pid:
      switch (item) {
        case 0:
          strlcpy(gRt.settingsLabel, "KP", sizeof(gRt.settingsLabel));
          snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%.2f", gCfg.pidKp);
          break;
        case 1:
          strlcpy(gRt.settingsLabel, "KI", sizeof(gRt.settingsLabel));
          snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%.3f", gCfg.pidKi);
          break;
        case 2:
          strlcpy(gRt.settingsLabel, "KD", sizeof(gRt.settingsLabel));
          snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%.2f", gCfg.pidKd);
          break;
        case 3:
          strlcpy(gRt.settingsLabel, "DIRECTION", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, gCfg.pidDirection == PidDirection::Reverse ? "REVERSE" : "DIRECT", sizeof(gRt.settingsValue));
          break;
        case 4:
          strlcpy(gRt.settingsLabel, "OUTPUT LIMIT", sizeof(gRt.settingsLabel));
          snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%.0f %%", gCfg.maxOutputPercent);
          break;
        case 5:
          strlcpy(gRt.settingsLabel, "AUTOTUNE", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, "START", sizeof(gRt.settingsValue));
          break;
        default:
          strlcpy(gRt.settingsLabel, "BACK", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, "SELECT", sizeof(gRt.settingsValue));
          break;
      }
      break;

    case SettingsSection::Network:
      switch (item) {
        case 0:
          strlcpy(gRt.settingsLabel, "WI-FI STATUS", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, gRt.wifiConnected ? WiFi.SSID().c_str() : "DISCONNECTED", sizeof(gRt.settingsValue));
          break;
        case 1:
          strlcpy(gRt.settingsLabel, "IP ADDRESS", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, gRt.wifiConnected ? WiFi.localIP().toString().c_str() : "--", sizeof(gRt.settingsValue));
          break;
        case 2:
          strlcpy(gRt.settingsLabel, "MQTT HOST", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, gBinding.brokerHost[0] ? gBinding.brokerHost : gCfg.mqttHost, sizeof(gRt.settingsValue));
          break;
        case 3:
          strlcpy(gRt.settingsLabel, "MQTT PORT", sizeof(gRt.settingsLabel));
          snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%u", gBinding.brokerPort ? gBinding.brokerPort : gCfg.mqttPort);
          break;
        case 4:
          strlcpy(gRt.settingsLabel, "CLEAR WI-FI", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, "SELECT", sizeof(gRt.settingsValue));
          break;
        default:
          strlcpy(gRt.settingsLabel, "BACK", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, "SELECT", sizeof(gRt.settingsValue));
          break;
      }
      break;

    case SettingsSection::Integration:
      switch (item) {
        case 0:
          strlcpy(gRt.settingsLabel, "PAIRED SYSTEM", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, gRt.systemName[0] ? gRt.systemName : "UNBOUND", sizeof(gRt.settingsValue));
          break;
        case 1:
          strlcpy(gRt.settingsLabel, "CONTROLLER ID", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, gRt.controllerId[0] ? gRt.controllerId : "NONE", sizeof(gRt.settingsValue));
          break;
        case 2:
          strlcpy(gRt.settingsLabel, "CONTROLLER LINK", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, gRt.controllerConnected ? "CONNECTED" : "OFFLINE", sizeof(gRt.settingsValue));
          break;
        case 3:
          strlcpy(gRt.settingsLabel, "UNPAIR", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, "SELECT", sizeof(gRt.settingsValue));
          break;
        default:
          strlcpy(gRt.settingsLabel, "BACK", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, "SELECT", sizeof(gRt.settingsValue));
          break;
      }
      break;

    case SettingsSection::Device:
      switch (item) {
        case 0:
          strlcpy(gRt.settingsLabel, "BRIGHTNESS", sizeof(gRt.settingsLabel));
          snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%u", gCfg.displayBrightness);
          break;
        case 1:
          strlcpy(gRt.settingsLabel, "BUZZER", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, gCfg.buzzerEnabled ? "ON" : "OFF", sizeof(gRt.settingsValue));
          break;
        case 2:
          strlcpy(gRt.settingsLabel, "CAL OFFSET", sizeof(gRt.settingsLabel));
          snprintf(gRt.settingsValue, sizeof(gRt.settingsValue), "%.2f C", gCfg.tempOffsetC);
          break;
        case 3:
          strlcpy(gRt.settingsLabel, "DEVICE ID", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, gCfg.deviceId, sizeof(gRt.settingsValue));
          break;
        case 4:
          strlcpy(gRt.settingsLabel, "FIRMWARE", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, CoreConfig::FIRMWARE_VERSION, sizeof(gRt.settingsValue));
          break;
        case 5:
          strlcpy(gRt.settingsLabel, "RESET WI-FI", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, "SELECT", sizeof(gRt.settingsValue));
          break;
        default:
          strlcpy(gRt.settingsLabel, "BACK", sizeof(gRt.settingsLabel));
          strlcpy(gRt.settingsValue, "SELECT", sizeof(gRt.settingsValue));
          break;
      }
      break;

    case SettingsSection::Exit:
    default:
      strlcpy(gRt.settingsLabel, "EXIT", sizeof(gRt.settingsLabel));
      strlcpy(gRt.settingsValue, "SELECT", sizeof(gRt.settingsValue));
      break;
  }
}

static void enterSettingsMode() {
  gRt.uiMode = UiMode::SettingsAdjust;
  gRt.settingsMenuLevel = SettingsMenuLevel::SectionList;
  gRt.settingsSection = SettingsSection::Status;
  gRt.settingsItemIndex = 0;
  refreshSettingsUiText();
}

static void leaveSettingsMode() {
  if (gRt.runState == RunState::Running) gRt.uiMode = UiMode::Running;
  else if (gRt.runState == RunState::Paused) gRt.uiMode = UiMode::Paused;
  else gRt.uiMode = UiMode::SetpointAdjust;

  gRt.settingsMenuLevel = SettingsMenuLevel::SectionList;
  gRt.settingsItemIndex = 0;
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

static void persistAndPublishConfig() {
  applyPersistentRuntimeSettings();
  gStorage.save(gCfg);
  if (gRt.mqttConnected) gMqtt.publishConfig(gCfg, gRt);
}

static bool settingsItemIsEditable(SettingsSection section, uint8_t item) {
  switch (section) {
    case SettingsSection::Control:
      return item <= 7;
    case SettingsSection::Pid:
      return item <= 4;
    case SettingsSection::Device:
      return item <= 2;
    default:
      return false;
  }
}

static bool settingsItemIsAction(SettingsSection section, uint8_t item) {
  switch (section) {
    case SettingsSection::Control:
      return item >= 8;
    case SettingsSection::Pid:
      return item >= 5;
    case SettingsSection::Network:
      return item >= 4;
    case SettingsSection::Integration:
      return item >= 3;
    case SettingsSection::Device:
      return item >= 5;
    case SettingsSection::Exit:
      return true;
    default:
      return item == settingsItemCount(section) - 1;
  }
}

static void adjustCurrentSetting(int32_t diff) {
  if (diff == 0) return;

  bool changed = false;
  switch (gRt.settingsSection) {
    case SettingsSection::Control:
      switch (gRt.settingsItemIndex) {
        case 0:
          gCfg.localSetpointC = clampSetpointToLimits(gCfg.localSetpointC + diff * 0.5f);
          gRt.currentSetpointC = gCfg.localSetpointC;
          changed = true;
          break;
        case 1:
          gCfg.controlEnabled = diff > 0;
          changed = true;
          break;
        case 2:
          if (gRt.operatingMode == OperatingMode::Integrated) {
            applyLocalAuthorityOverride(diff > 0);
            changed = true;
          }
          break;
        case 3:
          gCfg.minSetpointC = constrain(gCfg.minSetpointC + diff * 0.5f, 0.0f, 140.0f);
          changed = true;
          break;
        case 4:
          gCfg.maxSetpointC = constrain(gCfg.maxSetpointC + diff * 0.5f, 0.0f, 140.0f);
          changed = true;
          break;
        case 5:
          gCfg.lowAlarmC = constrain(gCfg.lowAlarmC + diff * 0.5f, -20.0f, 140.0f);
          changed = true;
          break;
        case 6:
          gCfg.highAlarmC = constrain(gCfg.highAlarmC + diff * 0.5f, -20.0f, 140.0f);
          changed = true;
          break;
        case 7:
          gCfg.tempAlarmEnabled = diff > 0;
          changed = true;
          break;
        default:
          break;
      }
      break;

    case SettingsSection::Pid:
      switch (gRt.settingsItemIndex) {
        case 0:
          gCfg.pidKp = constrain(gCfg.pidKp + diff * 0.2f, 0.1f, 60.0f);
          changed = true;
          break;
        case 1:
          gCfg.pidKi = constrain(gCfg.pidKi + diff * 0.01f, 0.001f, 2.0f);
          changed = true;
          break;
        case 2:
          gCfg.pidKd = constrain(gCfg.pidKd + diff * 0.2f, 0.1f, 80.0f);
          changed = true;
          break;
        case 3:
          gCfg.pidDirection = (diff > 0) ? PidDirection::Reverse : PidDirection::Direct;
          changed = true;
          break;
        case 4:
          gCfg.maxOutputPercent = constrain(gCfg.maxOutputPercent + diff * 5.0f, 0.0f, 100.0f);
          changed = true;
          break;
        default:
          break;
      }
      break;

    case SettingsSection::Device:
      switch (gRt.settingsItemIndex) {
        case 0:
          gCfg.displayBrightness = static_cast<uint8_t>(constrain(static_cast<int>(gCfg.displayBrightness) + static_cast<int>(diff * 8), 8, 255));
          changed = true;
          break;
        case 1:
          gCfg.buzzerEnabled = diff > 0;
          changed = true;
          break;
        case 2:
          gCfg.tempOffsetC = constrain(gCfg.tempOffsetC + diff * 0.1f, -10.0f, 10.0f);
          gTempSensor.setCalibrationOffset(gCfg.tempOffsetC);
          changed = true;
          break;
        default:
          break;
      }
      break;

    default:
      break;
  }

  if (changed) {
    applyTunings(gCfg.pidKp, gCfg.pidKi, gCfg.pidKd);
    persistAndPublishConfig();
  }
}

static void activateCurrentSettingsSelection() {
  if (gRt.settingsMenuLevel == SettingsMenuLevel::SectionList) {
    if (gRt.settingsSection == SettingsSection::Exit) {
      leaveSettingsMode();
    } else {
      gRt.settingsMenuLevel = SettingsMenuLevel::ItemList;
      gRt.settingsItemIndex = 0;
    }
    refreshSettingsUiText();
    return;
  }

  if (gRt.settingsMenuLevel == SettingsMenuLevel::EditValue) {
    gRt.settingsMenuLevel = SettingsMenuLevel::ItemList;
    refreshSettingsUiText();
    return;
  }

  if (settingsItemIsAction(gRt.settingsSection, gRt.settingsItemIndex)) {
    switch (gRt.settingsSection) {
      case SettingsSection::Status:
        gRt.settingsMenuLevel = SettingsMenuLevel::SectionList;
        gRt.settingsItemIndex = 0;
        break;
      case SettingsSection::Control:
        if (gRt.settingsItemIndex == 8) {
          gStages.stop();
          gCompletionHandled = false;
          logRuntimeEvent("Run stopped (menu)");
        } else {
          gRt.settingsMenuLevel = SettingsMenuLevel::SectionList;
          gRt.settingsItemIndex = 0;
        }
        break;
      case SettingsSection::Pid:
        if (gRt.settingsItemIndex == 5) {
          if (gRt.operatingMode == OperatingMode::Integrated && !gCfg.localAuthorityOverride) {
            applyLocalAuthorityOverride(true);
            logRuntimeEvent("Local override enabled for autotune");
            persistAndPublishConfig();
          }
          startAutoTune();
          logRuntimeEvent("Autotune started (menu)");
        } else {
          gRt.settingsMenuLevel = SettingsMenuLevel::SectionList;
          gRt.settingsItemIndex = 0;
        }
        break;
      case SettingsSection::Network:
        if (gRt.settingsItemIndex == 4) {
          gWifi.resetSettings();
          logRuntimeEvent("WiFi reset (menu)");
        } else {
          gRt.settingsMenuLevel = SettingsMenuLevel::SectionList;
          gRt.settingsItemIndex = 0;
        }
        break;
      case SettingsSection::Integration:
        if (gRt.settingsItemIndex == 3) {
          gIntegration.unpairToStandalone(true);
          gBinding = gIntegration.binding();
          applyLocalAuthorityOverride(false);
          logRuntimeEvent("Device unpaired");
        } else {
          gRt.settingsMenuLevel = SettingsMenuLevel::SectionList;
          gRt.settingsItemIndex = 0;
        }
        break;
      case SettingsSection::Device:
        if (gRt.settingsItemIndex == 5) {
          gWifi.resetSettings();
          logRuntimeEvent("WiFi reset (device menu)");
        } else {
          gRt.settingsMenuLevel = SettingsMenuLevel::SectionList;
          gRt.settingsItemIndex = 0;
        }
        break;
      case SettingsSection::Exit:
      default:
        leaveSettingsMode();
        break;
    }
    refreshSettingsUiText();
    return;
  }

  if (settingsItemIsEditable(gRt.settingsSection, gRt.settingsItemIndex)) {
    gRt.settingsMenuLevel = SettingsMenuLevel::EditValue;
    refreshSettingsUiText();
  }
}

static void debugPrintBootNetworkTargets() {
  String ssidStr = WiFi.SSID();
  const char* ssid = (ssidStr.length() > 0) ? ssidStr.c_str() : "<not-associated-yet>";
  const char* mqttHost = (gBinding.brokerHost[0] != '\0') ? gBinding.brokerHost : gCfg.mqttHost;
  const uint16_t mqttPort = (gBinding.brokerPort != 0) ? gBinding.brokerPort : gCfg.mqttPort;

  IPAddress mqttIp;
  bool resolved = false;
  if (mqttHost[0] != '\0') {
    resolved = WiFi.hostByName(mqttHost, mqttIp);
  }

  DBG_PRINTF("Boot network target mode=%s SSID=%s\n",
             gRt.operatingMode == OperatingMode::Integrated ? "integrated" : "standalone",
             ssid);
  if (resolved) {
    DBG_PRINTF("Boot MQTT target host=%s resolved_ip=%s port=%u tls=%d\n",
               mqttHost,
               mqttIp.toString().c_str(),
               mqttPort,
               gCfg.mqttUseTls);
  } else {
    DBG_PRINTF("Boot MQTT target host=%s resolved_ip=<unresolved> port=%u tls=%d\n",
               mqttHost,
               mqttPort,
               gCfg.mqttUseTls);
  }
}

static void showBootInfoScreen(uint32_t durationMs = 30000) {
  String ssid = WiFi.SSID();
  if (ssid.length() == 0) ssid = "<not-associated>";
  const char* mqttHost = (gBinding.brokerHost[0] != '\0') ? gBinding.brokerHost : gCfg.mqttHost;
  const uint16_t mqttPort = (gBinding.brokerPort != 0) ? gBinding.brokerPort : gCfg.mqttPort;

  IPAddress mqttIp;
  const bool mqttResolved = (mqttHost[0] != '\0') && WiFi.hostByName(mqttHost, mqttIp);
  const String mqttIpText = mqttResolved ? mqttIp.toString() : String("<unresolved>");
  const String mqttTopicBase = gMqtt.topicBase();

  auto& d = M5Dial.Display;
  d.fillScreen(BLACK);
  d.setTextColor(WHITE, BLACK);
  d.setTextDatum(top_left);
  d.setFont(&fonts::Font2);
  d.drawString("Environment Controller", 8, 8);

  d.setTextColor(GOLD, BLACK);
  d.drawString(String("FW: ") + CoreConfig::FIRMWARE_VERSION, 8, 34);

  d.setTextColor(CYAN, BLACK);
  d.drawString(String("Mode: ") + (gRt.operatingMode == OperatingMode::Integrated ? "integrated" : "standalone"), 8, 56);
  d.drawString(String("SSID: ") + ssid, 8, 80);

  d.setTextColor(WHITE, BLACK);
  d.drawString(String("MQTT Host: ") + mqttHost, 8, 104);
  d.drawString(String("MQTT IP: ") + mqttIpText, 8, 128);
  d.drawString(String("Port/TLS: ") + String(mqttPort) + (gCfg.mqttUseTls ? " / on" : " / off"), 8, 152);
  d.drawString(String("Client ID: ") + gMqtt.clientId(), 8, 176);
  d.drawString(String("Topic Base: ") + mqttTopicBase, 8, 200);

  d.setTextColor(0xBDF7, BLACK);
  d.drawString("Press button to continue...", 8, 224);

  const uint32_t started = millis();
  while (millis() - started < durationMs) {
    M5Dial.update();
    if (M5Dial.BtnA.wasClicked()) break;
    delay(20);
  }
}

static bool shouldRunUnpairResetOnBootHold(uint32_t promptWindowMs = 4000, uint32_t holdThresholdMs = 1200) {
  auto& d = M5Dial.Display;
  d.fillScreen(BLACK);
  d.setTextColor(ORANGE, BLACK);
  d.setTextDatum(top_left);
  d.setFont(&fonts::Font2);
  d.drawString("Hold button to unpair/reset", 8, 88);
  d.setTextColor(WHITE, BLACK);
  d.drawString("Clears integrated binding", 8, 112);
  d.drawString("and WiFi credentials", 8, 136);

  const uint32_t started = millis();
  uint32_t pressedAt = 0;
  bool pressed = false;
  while (millis() - started < promptWindowMs) {
    M5Dial.update();
    if (M5Dial.BtnA.wasPressed()) {
      pressed = true;
      pressedAt = millis();
    }
    if (M5Dial.BtnA.wasReleased()) {
      pressed = false;
      pressedAt = 0;
    }
    if (M5Dial.BtnA.wasHold()) {
      playUiTone(2600, 80);
      return true;
    }
    if (pressed && pressedAt != 0 && millis() - pressedAt >= holdThresholdMs) {
      playUiTone(2600, 80);
      return true;
    }
    delay(20);
  }
  return false;
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
      activateCurrentSettingsSelection();
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
    if (gRt.autoTunePhase == AutoTunePhase::PendingAccept) {
      applyTunings(gCfg.pidKp, gCfg.pidKi, gCfg.pidKd);
      gRt.autoTunePhase = AutoTunePhase::Failed;
      gRt.autoTuneQualityScore = 0.0f;
      logRuntimeEvent("Autotune rejected (local)");
      gDisplay.invalidateAll();
      playUiTone(2200, 80);
      return;
    }

    if (gRt.uiMode != UiMode::SettingsAdjust) {
      enterSettingsMode();
      logRuntimeEvent("Settings opened");
    }
    gDisplay.invalidateAll();
    playUiTone(2200, 80);
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
    playUiTone(3200, 20);
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

  if (gRt.uiMode == UiMode::SetpointAdjust && localRuntimeAuthorityActive()) {
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
    if (gRt.settingsMenuLevel == SettingsMenuLevel::SectionList) {
      int nextSection = static_cast<int>(gRt.settingsSection) + (diff > 0 ? 1 : -1);
      while (nextSection < static_cast<int>(SettingsSection::Status)) nextSection += 7;
      while (nextSection > static_cast<int>(SettingsSection::Exit)) nextSection -= 7;
      gRt.settingsSection = static_cast<SettingsSection>(nextSection);
      gRt.settingsItemIndex = 0;
    } else if (gRt.settingsMenuLevel == SettingsMenuLevel::ItemList) {
      int nextItem = static_cast<int>(gRt.settingsItemIndex) + (diff > 0 ? 1 : -1);
      const int count = settingsItemCount(gRt.settingsSection);
      while (nextItem < 0) nextItem += count;
      while (nextItem >= count) nextItem -= count;
      gRt.settingsItemIndex = static_cast<uint8_t>(nextItem);
    } else {
      adjustCurrentSetting(diff);
    }
    refreshSettingsUiText();
  } else if (gDebugVerboseInput) {
    DBG_PRINTF("Encoder diff=%ld ignored ui=%u\n", (long)diff, (unsigned)gRt.uiMode);
  }

  gDisplay.requestImmediateUi();
}

static void updateTemperatureAlertState() {
  gRt.lowTempAlarmActive = false;
  gRt.highTempAlarmActive = false;

  if (!gCfg.tempAlarmEnabled || !gRt.sensorHealthy || isnan(gRt.currentTempC)) return;

  if (gRt.currentTempC <= gCfg.lowAlarmC) {
    gRt.lowTempAlarmActive = true;
  }
  if (gRt.currentTempC >= gCfg.highAlarmC) {
    gRt.highTempAlarmActive = true;
  }
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
    updateTemperatureAlertState();
    if (gCfg.tempAlarmEnabled && gRt.lowTempAlarmActive) {
      gAlarm.setAlarm(AlarmCode::LowProcessTemp, alarmText(AlarmCode::LowProcessTemp), false);
    } else if (gCfg.tempAlarmEnabled && gRt.highTempAlarmActive) {
      gAlarm.setAlarm(AlarmCode::HighProcessTemp, alarmText(AlarmCode::HighProcessTemp), false);
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
  DBG_BEGIN(115200);
  delay(50);
  debugLogBanner();

  auto cfg = M5.config();
  M5Dial.begin(cfg, true, false);
  M5Dial.Display.setRotation(0);

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

  if (!debugWifiDisabledEffective()) {
    if (shouldRunUnpairResetOnBootHold()) {
      DBG_LOGLN("Boot button hold detected: unpair/reset");
      gIntegration.unpairToStandalone(true);
      logRuntimeEvent("Integrated binding cleared (boot hold)");
      M5Dial.Display.fillScreen(BLACK);
      M5Dial.Display.setTextColor(RED, BLACK);
      M5Dial.Display.setTextDatum(top_left);
      M5Dial.Display.setFont(&fonts::Font2);
      M5Dial.Display.drawString("Standalone reset complete", 8, 92);
      M5Dial.Display.drawString("Opening local setup...", 8, 116);
      delay(600);
    }
    if (gIntegration.shouldBootIntegratedNetworking()) {
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
    DBG_PRINTF("WiFi begin done mqttHost=%s mqttPort=%u timeout=%u\n",
               gBinding.brokerHost[0] != '\0' ? gBinding.brokerHost : gCfg.mqttHost,
               gBinding.brokerPort != 0 ? gBinding.brokerPort : gCfg.mqttPort,
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
  } else {
    DBG_LOGLN("MQTT disabled by debug/compile-time network mode");
    gRt.mqttConnected = false;
  }

  showBootInfoScreen();
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
  syncControlAuthority();

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

  gDisplay.draw(gCfg, gRt, stage, remaining);
  delay(1);
}
