#pragma once

#include <ArduinoJson.h>

#include "AlarmManager.h"
#include "AppState.h"
#include "DisplayManager.h"
#include "MqttManager.h"
#include "StageManager.h"
#include "StorageManager.h"
#include "TempSensor.h"
#include "WifiManagerWrapper.h"

struct CommandRouterServices {
  PersistentConfig& cfg;
  RuntimeState& rt;
  StorageManager& storage;
  StageManager& stages;
  MqttManager& mqtt;
  DisplayManager& display;
  AlarmManager& alarm;
  TempSensor& tempSensor;
  WifiManagerWrapper& wifi;
  bool& completionHandled;
  bool (*candidateWithinGuardrails)(float kp, float ki, float kd);
  void (*applyTunings)(float kp, float ki, float kd);
  bool (*startAutoTune)();
  void (*persistActivePidAndQuality)();
  void (*logRuntimeEvent)(const char* text);
  void (*syncAlarmFromManager)();
  void (*clearStoredNetworking)();
  bool (*upsertProfileFromJson)(const JsonDocument& doc, uint8_t* outIndex);
  void (*applyLocalAuthorityOverride)(bool enabled);
  const char* (*startInterlockReason)(bool profileRun);
  void (*publishLifecycleEvent)(const char* type, const char* detail, const char* cause);
};

void routeMqttCommand(const char* topic, const char* payload, CommandRouterServices& services);
