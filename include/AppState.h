#pragma once

#include <Arduino.h>
#include "core/CoreConfig.h"

enum class ControlLock : uint8_t {
  LocalOnly = 0,
  RemoteOnly,
  LocalOrRemote
};

enum class ControlMode : uint8_t {
  Local = 0,
  Remote
};

enum class MqttFallbackMode : uint8_t {
  HoldSetpoint = 0,
  Pause,
  StopHeater
};

enum class RunState : uint8_t {
  Idle = 0,
  Running,
  Paused,
  Complete,
  Fault,
  AutoTune
};

enum class AutoTunePhase : uint8_t {
  Inactive = 0,
  Stabilizing,
  Perturbing,
  Settling,
  PendingAccept,
  Complete,
  Failed
};

enum class UiMode : uint8_t {
  SetpointAdjust = 0,
  StageTimeAdjust,
  Running,
  Paused,
  SettingsAdjust
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
  BrewStage stages[CoreConfig::MAX_STAGES];
};

struct RuntimeEvent {
  uint32_t atMs {0};
  char text[48] {""};
};

struct PersistentConfig {
  ControlLock controlLock {ControlLock::LocalOrRemote};
  float localSetpointC {CoreConfig::DEFAULT_SETPOINT_C};
  uint32_t manualStageMinutes {CoreConfig::DEFAULT_STAGE_MINUTES};
  float stageStartBandC {CoreConfig::DEFAULT_STAGE_START_BAND_C};
  float overTempC {CoreConfig::DEFAULT_OVER_TEMP_C};
  float tempOffsetC {0.0f};
  float tempSmoothingAlpha {CoreConfig::DEFAULT_TEMP_SMOOTHING_ALPHA};
  char mqttHost[64] {"192.168.1.10"};
  uint16_t mqttPort {CoreConfig::MQTT_PORT_PLAIN};
  char mqttUser[32] {""};
  char mqttPass[32] {""};
  bool mqttUseTls {false};
  uint8_t mqttTlsAuthMode {0};  // 0=none, 1=fingerprint pin, 2=CA cert pin
  char mqttTlsFingerprint[96] {""};  // e.g. AA:BB:...
  char mqttTlsCaCert[768] {""};      // PEM
  uint16_t mqttCommsTimeoutSec {0};
  MqttFallbackMode mqttFallbackMode {MqttFallbackMode::HoldSetpoint};
  uint16_t wifiPortalTimeoutSec {CoreConfig::DEFAULT_WIFI_PORTAL_TIMEOUT_SEC};
  float pidKp {CoreConfig::PID_KP};
  float pidKi {CoreConfig::PID_KI};
  float pidKd {CoreConfig::PID_KD};
  float prevPidKp {CoreConfig::PID_KP};
  float prevPidKi {CoreConfig::PID_KI};
  float prevPidKd {CoreConfig::PID_KD};
  float tuneQualityScore {0.0f};
  BrewProfile profiles[CoreConfig::MAX_PROFILES];
  uint8_t profileCount {0};
  uint8_t activeProfileIndex {0};
};

struct RuntimeState {
  ControlMode controlMode {ControlMode::Local};
  RunState runState {RunState::Idle};
  UiMode uiMode {UiMode::SetpointAdjust};

  float currentTempC {NAN};
  float currentRawTempC {NAN};
  float currentSetpointC {CoreConfig::DEFAULT_SETPOINT_C};
  float heaterOutputPct {0.0f};

  bool wifiConnected {false};
  bool mqttConnected {false};
  bool sensorHealthy {false};
  bool tempPlausible {true};
  bool heatingEnabled {false};
  bool stageTimerStarted {false};
  bool pendingProfileCompletePublish {false};
  bool heatOn {false};
  uint32_t lastValidMqttConnectionAtMs {0};
  uint32_t lastAcceptedRemoteCommandAtMs {0};

  uint8_t currentStageIndex {0};
  uint32_t activeStageMinutes {CoreConfig::DEFAULT_STAGE_MINUTES};
  float desiredSetpointC {CoreConfig::DEFAULT_SETPOINT_C};
  uint32_t desiredMinutes {CoreConfig::DEFAULT_STAGE_MINUTES};
  char desiredRunAction[16] {"stop"};
  uint32_t stageStartedAtMs {0};
  uint32_t stageHoldStartedAtMs {0};

  AutoTunePhase autoTunePhase {AutoTunePhase::Inactive};
  float autoTuneRiseTimeSec {0.0f};
  float autoTuneOvershootC {0.0f};
  float autoTuneSettlingSec {0.0f};
  float autoTuneQualityScore {0.0f};
  float currentKp {CoreConfig::PID_KP};
  float currentKi {CoreConfig::PID_KI};
  float currentKd {CoreConfig::PID_KD};
  float previousKp {CoreConfig::PID_KP};
  float previousKi {CoreConfig::PID_KI};
  float previousKd {CoreConfig::PID_KD};

  AlarmCode activeAlarm {AlarmCode::None};
  char alarmText[64] {"OK"};
  char settingsLabel[24] {""};
  char settingsValue[48] {""};
  RuntimeEvent eventLog[CoreConfig::EVENT_LOG_CAPACITY];
  uint8_t eventLogHead {0};
  uint8_t eventLogCount {0};
  bool pendingEventLogPublish {false};
};
