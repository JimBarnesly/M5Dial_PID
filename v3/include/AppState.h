#pragma once

#include <Arduino.h>
#include "Config.h"

enum class ControlLock : uint8_t {
  LocalOnly = 0,
  RemoteOnly,
  LocalOrRemote
};

enum class ControlMode : uint8_t {
  Local = 0,
  Remote
};

enum class RunState : uint8_t {
  Idle = 0,
  Running,
  Paused,
  Complete,
  Fault
};

enum class AlarmCode : uint8_t {
  None = 0,
  SensorFault,
  OverTemp,
  HeatingIneffective,
  MqttOffline
};

struct BrewStage {
  char name[20];
  float targetC {0.0f};
  uint32_t holdSeconds {0};
};

struct BrewProfile {
  char name[24];
  uint8_t stageCount {0};
  BrewStage stages[Config::MAX_STAGES];
};

struct PersistentConfig {
  ControlLock controlLock {ControlLock::LocalOrRemote};
  float localSetpointC {Config::DEFAULT_SETPOINT_C};
  float stageStartBandC {Config::DEFAULT_STAGE_START_BAND_C};
  float overTempC {Config::DEFAULT_OVER_TEMP_C};
  char mqttHost[64] {"192.168.1.10"};
  uint16_t mqttPort {1883};
  char mqttUser[32] {""};
  char mqttPass[32] {""};
  BrewProfile profiles[Config::MAX_PROFILES];
  uint8_t profileCount {0};
  uint8_t activeProfileIndex {0};
};

struct RuntimeState {
  ControlMode controlMode {ControlMode::Local};
  RunState runState {RunState::Idle};

  float currentTempC {NAN};
  float currentSetpointC {Config::DEFAULT_SETPOINT_C};
  float heaterOutputPct {0.0f};

  bool wifiConnected {false};
  bool mqttConnected {false};
  bool sensorHealthy {false};
  bool heatingEnabled {false};
  bool editSetpointMode {false};
  bool stageTimerStarted {false};
  bool pendingProfileCompletePublish {false};

  uint8_t currentStageIndex {0};
  uint32_t stageStartedAtMs {0};
  uint32_t stageHoldStartedAtMs {0};

  AlarmCode activeAlarm {AlarmCode::None};
  char alarmText[64] {"OK"};
};
