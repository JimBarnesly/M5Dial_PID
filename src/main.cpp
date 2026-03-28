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

uint32_t gLastStatusMs = 0;
uint32_t gLastPidMs = 0;
uint32_t gHeatEvalWindowStart = 0;
float gHeatEvalStartTemp = NAN;
bool gCompletionHandled = false;

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
  if (gAlarm.getAlarm() == AlarmCode::SensorFault && gRt.sensorHealthy) {
    gAlarm.clearAlarm();
  }
  if (gAlarm.getAlarm() == AlarmCode::OverTemp && gRt.currentTempC < (gCfg.overTempC - 1.0f)) {
    gAlarm.clearAlarm();
  }
  if (gAlarm.getAlarm() == AlarmCode::HeatingIneffective && !isnan(gHeatEvalStartTemp) && gRt.currentTempC >= gHeatEvalStartTemp + Config::MIN_EXPECTED_RISE_C) {
    gAlarm.clearAlarm();
  }
  syncAlarmFromManager();
}

void handleCommands(const char* topic, const char* payload) {
  String t(topic);
  JsonDocument doc;
  deserializeJson(doc, payload);

  if (t.endsWith("/cmd/setpoint")) {
    if (gCfg.controlLock == ControlLock::RemoteOnly || gCfg.controlLock == ControlLock::LocalOrRemote) {
      gRt.controlMode = ControlMode::Remote;
      gRt.currentSetpointC = doc["setpointC"] | atof(payload);
      gDisplay.invalidateAll();
    }
  } else if (t.endsWith("/cmd/control_lock")) {
    String v = doc["value"] | payload;
    if (v == "local_only") gCfg.controlLock = ControlLock::LocalOnly;
    else if (v == "remote_only") gCfg.controlLock = ControlLock::RemoteOnly;
    else gCfg.controlLock = ControlLock::LocalOrRemote;
    gStorage.save(gCfg);
    gDisplay.invalidateAll();
  } else if (t.endsWith("/cmd/mode")) {
    String v = doc["value"] | payload;
    if (v == "local" && gCfg.controlLock != ControlLock::RemoteOnly) gRt.controlMode = ControlMode::Local;
    if (v == "remote" && gCfg.controlLock != ControlLock::LocalOnly) gRt.controlMode = ControlMode::Remote;
    gDisplay.invalidateAll();
  } else if (t.endsWith("/cmd/profile")) {
    uint8_t idx = doc["index"] | atoi(payload);
    if (idx < gCfg.profileCount) {
      gCfg.activeProfileIndex = idx;
      gStorage.save(gCfg);
      gDisplay.invalidateAll();
    }
  } else if (t.endsWith("/cmd/start")) {
    gStages.startProfile(gCfg.activeProfileIndex);
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
  } else if (t.endsWith("/cmd/profile_json")) {
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      JsonObject p = doc["profile"].as<JsonObject>();
      if (!p.isNull()) {
        uint8_t idx = doc["index"] | gCfg.activeProfileIndex;
        if (idx < Config::MAX_PROFILES) {
          BrewProfile& dst = gCfg.profiles[idx];
          memset(&dst, 0, sizeof(dst));
          strlcpy(dst.name, p["name"] | "Profile", sizeof(dst.name));
          for (JsonObject s : p["stages"].as<JsonArray>()) {
            if (dst.stageCount >= Config::MAX_STAGES) break;
            BrewStage& st = dst.stages[dst.stageCount++];
            strlcpy(st.name, s["name"] | "Stage", sizeof(st.name));
            st.targetC = s["targetC"] | 65.0f;
            st.holdSeconds = s["holdSeconds"] | 600;
          }
          if (idx >= gCfg.profileCount) gCfg.profileCount = idx + 1;
          gStorage.save(gCfg);
          gDisplay.invalidateAll();
        }
      }
    }
  }
}

void processInput() {
  M5Dial.update();

  if (M5Dial.BtnA.wasClicked()) {
    gRt.editSetpointMode = !gRt.editSetpointMode;
    gDisplay.invalidateAll();
  }

  const int32_t enc = M5Dial.Encoder.read();
  static int32_t lastEnc = enc;
  const int32_t diff = enc - lastEnc;
  lastEnc = enc;

  if (gRt.editSetpointMode && diff != 0 && gCfg.controlLock != ControlLock::RemoteOnly) {
    gRt.controlMode = ControlMode::Local;
    gCfg.localSetpointC = constrain(gCfg.localSetpointC + diff * 1.0f, 20.0f, 100.0f);
    gRt.currentSetpointC = gCfg.localSetpointC;
  }
}

void updateSafety() {
  gRt.sensorHealthy = gTempSensor.isHealthy();

  if (!gRt.sensorHealthy) {
    gAlarm.setAlarm(AlarmCode::SensorFault, alarmText(AlarmCode::SensorFault));
    gRt.runState = RunState::Fault;
  } else if (gRt.currentTempC >= gCfg.overTempC) {
    gAlarm.setAlarm(AlarmCode::OverTemp, alarmText(AlarmCode::OverTemp));
    gRt.runState = RunState::Fault;
  } else {
    clearFaultIfRecoverable();
    if (gRt.runState == RunState::Fault && gAlarm.getAlarm() == AlarmCode::None) {
      gRt.runState = RunState::Idle;
    }
  }

  if (gHeater.getOutputPercent() > 20.0f && !isnan(gRt.currentTempC)) {
    if (gHeatEvalWindowStart == 0) {
      gHeatEvalWindowStart = millis();
      gHeatEvalStartTemp = gRt.currentTempC;
    } else if (millis() - gHeatEvalWindowStart >= Config::TEMP_RISE_EVAL_MS) {
      if (gRt.currentTempC < gHeatEvalStartTemp + Config::MIN_EXPECTED_RISE_C) {
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

  if (gRt.controlMode == ControlMode::Local) {
    gRt.currentSetpointC = gCfg.localSetpointC;
  }

  if (gRt.runState == RunState::Running) {
    gStages.update(gRt.currentTempC);
  }

  gRt.heatingEnabled = true;
  gRt.heaterOutputPct = gPid.compute(gRt.currentSetpointC, gRt.currentTempC, dt);
  gHeater.setEnabled(true);
  gHeater.setOutputPercent(gRt.heaterOutputPct);
}

void setup() {
  auto cfg = M5.config();
  M5Dial.begin(cfg, true, false);
  M5Dial.Display.setRotation(0);

  gDisplay.begin();
  gStorage.begin();
  gStorage.load(gCfg);

  gRt.currentSetpointC = gCfg.localSetpointC;
  gRt.controlMode = (gCfg.controlLock == ControlLock::RemoteOnly) ? ControlMode::Remote : ControlMode::Local;

  gTempSensor.begin();
  gPid.begin(Config::PID_KP, Config::PID_KI, Config::PID_KD);
  gHeater.begin();
  gAlarm.begin();
  gStages.begin(&gCfg, &gRt);
  gWifi.begin();
  gMqtt.begin(&gCfg, &gRt);
  gMqtt.setCommandCallback(handleCommands);

  gDisplay.invalidateAll();
}

void loop() {
  processInput();

  gWifi.update();
  gRt.wifiConnected = gWifi.isConnected();

  gTempSensor.update();
  if (gTempSensor.hasNewValue()) {
    gRt.currentTempC = gTempSensor.getCelsius();
  }

  gMqtt.update();
  updateSafety();
  updateControl();
  gHeater.update();
  gAlarm.update();

  const BrewStage* stage = gStages.getCurrentStage();
  const uint32_t remaining = gStages.getRemainingSeconds();

  if (gRt.runState == RunState::Complete) {
    gHeater.setOutputPercent(0.0f);
    if (!gCompletionHandled) {
      gAlarm.notifyStageComplete();
      gRt.pendingProfileCompletePublish = true;
      gCompletionHandled = true;
      gDisplay.invalidateAll();
    }
    gMqtt.publishProfileCompleteIfPending(gRt);
  } else {
    gCompletionHandled = false;
  }

  if (millis() - gLastStatusMs >= Config::STATUS_PUBLISH_MS) {
    gLastStatusMs = millis();
    gMqtt.publishStatus(gRt, stage ? stage->name : "", remaining);
    gMqtt.publishProfileCompleteIfPending(gRt);
  }

  gDisplay.draw(gCfg, gRt, stage, remaining);
  delay(1);
}
