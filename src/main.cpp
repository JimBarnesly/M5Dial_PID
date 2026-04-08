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

static void debugLogBanner() {
  DBG_PRINTLN("\n=== BrewCore HLT V8 debug boot ===");
  DBG_PRINTF("DEBUG_ENABLED=%d WIFI=%d MQTT=%d\n",
             gDebugEnabled,
             !gDebugDisableWifi,
             !gDebugDisableMqtt);
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
  syncAlarmFromManager();
}

void handleCommands(const char* topic, const char* payload) {
  DBG_PRINTF("MQTT cmd topic=%s payload=%s\n", topic, payload);
  String t(topic);
  JsonDocument doc;
  deserializeJson(doc, payload);

  if (t.endsWith("/cmd/setpoint")) {
    if (gCfg.controlLock != ControlLock::LocalOnly &&
        (gRt.uiMode == UiMode::SetpointAdjust || gRt.runState == RunState::Idle || gRt.runState == RunState::Complete)) {
      gRt.controlMode = ControlMode::Remote;
      gCfg.localSetpointC = doc["setpointC"] | atof(payload);
      gRt.currentSetpointC = gCfg.localSetpointC;
      gStorage.save(gCfg);
      gDisplay.invalidateAll();
    }
  } else if (t.endsWith("/cmd/minutes")) {
    gCfg.manualStageMinutes = max<uint32_t>(1, doc["minutes"] | atoi(payload));
    gRt.activeStageMinutes = gCfg.manualStageMinutes;
    gStorage.save(gCfg);
    gDisplay.invalidateAll();
  } else if (t.endsWith("/cmd/start")) {
    gStages.startProfile(0);
    gCompletionHandled = false;
    gDisplay.invalidateAll();
  } else if (t.endsWith("/cmd/pause")) {
    gStages.pause();
    gDisplay.invalidateAll();
  } else if (t.endsWith("/cmd/stop")) {
    gStages.stop();
    gCompletionHandled = false;
    gDisplay.invalidateAll();
  } else if (t.endsWith("/cmd/reset_alarm")) {
    gAlarm.clearAlarm();
    syncAlarmFromManager();
    gDisplay.invalidateAll();
  }
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
    if (mins < 1) mins = 1;
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
  gRt.controlMode = (gCfg.controlLock == ControlLock::RemoteOnly) ? ControlMode::Remote : ControlMode::Local;
  gRt.uiMode = UiMode::SetpointAdjust;

  gTempSensor.begin();
  gPid.begin(Config::PID_KP, Config::PID_KI, Config::PID_KD);
  gHeater.begin();
  gAlarm.begin();
  gStages.begin(&gCfg, &gRt);

  if (!gDebugDisableWifi) {
    gWifi.begin();
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

  static uint32_t lastDebugStateMs = 0;
  if (gDebugEnabled && now - lastDebugStateMs >= 2000) {
    lastDebugStateMs = now;
    debugPrintState("loop");
  }

  gDisplay.draw(gCfg, gRt, stage, remaining);
  delay(1);
}
