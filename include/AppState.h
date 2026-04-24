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

enum class ControlAuthority : uint8_t {
  Local = 0,
  Controller,
  LocalOverride
};

enum class OperatingMode : uint8_t {
  Standalone = 0,
  Integrated
};

enum class IntegrationState : uint8_t {
  None = 0,
  BootstrapPending,
  Enrolled
};

enum class DeviceType : uint8_t {
  ThermalController = 0,
  PumpController
};

enum class PidDirection : uint8_t {
  Direct = 0,
  Reverse
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

enum class SettingsSection : uint8_t {
  Status = 0,
  Control,
  Pid,
  Network,
  Integration,
  Device,
  Exit
};

enum class SettingsMenuLevel : uint8_t {
  SectionList = 0,
  ItemList,
  EditValue
};

enum class AlarmCode : uint8_t {
  None = 0,
  SensorFault,
  OverTemp,
  HeatingIneffective,
  MqttOffline,
  LowProcessTemp,
  HighProcessTemp
};

struct ProcessStage {
  char name[20];
  float targetC {0.0f};
  uint32_t holdSeconds {0};
};

struct ProcessProfile {
  char name[24];
  uint8_t stageCount {0};
  ProcessStage stages[CoreConfig::MAX_STAGES];
};

struct RuntimeEvent {
  uint32_t atMs {0};
  char text[48] {""};
};

struct IntegrationBinding {
  uint16_t schemaVersion {1};
  OperatingMode operatingMode {OperatingMode::Standalone};
  IntegrationState integrationState {IntegrationState::None};
  DeviceType deviceType {DeviceType::ThermalController};
  bool paired {false};
  char systemId[24] {"unbound"};
  char systemName[32] {""};
  char controllerId[24] {""};
  char controllerPublicKey[192] {""};
  char controllerFingerprint[96] {""};
  char apSsid[32] {""};
  char apPsk[64] {""};
  char brokerHost[64] {""};
  uint16_t brokerPort {0};
  uint32_t issuedAt {0};
  uint32_t epoch {0};
};

struct PersistentConfig {
  char systemId[24] {"unbound"};
  char deviceId[24] {""};
  ControlLock controlLock {ControlLock::LocalOrRemote};
  bool controlEnabled {true};
  bool localAuthorityOverride {false};
  float localSetpointC {CoreConfig::DEFAULT_SETPOINT_C};
  uint32_t manualStageMinutes {CoreConfig::DEFAULT_STAGE_MINUTES};
  float minSetpointC {CoreConfig::DEFAULT_MIN_SETPOINT_C};
  float maxSetpointC {CoreConfig::DEFAULT_MAX_SETPOINT_C};
  float stageStartBandC {CoreConfig::DEFAULT_STAGE_START_BAND_C};
  float overTempC {CoreConfig::DEFAULT_OVER_TEMP_C};
  bool tempAlarmEnabled {true};
  float lowAlarmC {CoreConfig::DEFAULT_LOW_ALARM_C};
  float highAlarmC {CoreConfig::DEFAULT_HIGH_ALARM_C};
  float alarmHysteresisC {CoreConfig::DEFAULT_ALARM_HYSTERESIS_C};
  float tempOffsetC {0.0f};
  float tempSmoothingAlpha {CoreConfig::DEFAULT_TEMP_SMOOTHING_ALPHA};
  char mqttHost[64] {"10.42.0.1"};
  uint16_t mqttPort {CoreConfig::MQTT_PORT_TLS};
  char mqttUser[32] {""};
  char mqttPass[32] {""};
  bool mqttUseTls {true};
  uint8_t mqttTlsAuthMode {0};  // 0=none, 1=fingerprint pin, 2=CA cert pin
  char mqttTlsFingerprint[96] {""};  // e.g. AA:BB:...
  char mqttTlsCaCert[768] {""};      // PEM
  uint16_t mqttCommsTimeoutSec {0};
  MqttFallbackMode mqttFallbackMode {MqttFallbackMode::HoldSetpoint};
  uint16_t wifiPortalTimeoutSec {CoreConfig::DEFAULT_WIFI_PORTAL_TIMEOUT_SEC};
  float pidKp {CoreConfig::PID_KP};
  float pidKi {CoreConfig::PID_KI};
  float pidKd {CoreConfig::PID_KD};
  PidDirection pidDirection {PidDirection::Direct};
  float maxOutputPercent {100.0f};
  float prevPidKp {CoreConfig::PID_KP};
  float prevPidKi {CoreConfig::PID_KI};
  float prevPidKd {CoreConfig::PID_KD};
  float tuneQualityScore {0.0f};
  uint8_t displayBrightness {128};
  bool buzzerEnabled {true};
  ProcessProfile profiles[CoreConfig::MAX_PROFILES];
  uint8_t profileCount {0};
  uint8_t activeProfileIndex {0};
};

struct RuntimeState {
  OperatingMode operatingMode {OperatingMode::Standalone};
  IntegrationState integrationState {IntegrationState::None};
  ControlMode controlMode {ControlMode::Local};
  ControlAuthority controlAuthority {ControlAuthority::Local};
  RunState runState {RunState::Idle};
  UiMode uiMode {UiMode::SetpointAdjust};
  SettingsSection settingsSection {SettingsSection::Status};
  SettingsMenuLevel settingsMenuLevel {SettingsMenuLevel::SectionList};

  float currentTempC {NAN};
  float currentRawTempC {NAN};
  float probeATempC {NAN};
  float probeARawTempC {NAN};
  float probeBTempC {NAN};
  float probeBRawTempC {NAN};
  float currentSetpointC {CoreConfig::DEFAULT_SETPOINT_C};
  float heaterOutputPct {0.0f};

  bool wifiConnected {false};
  bool mqttConnected {false};
  bool sensorHealthy {false};
  bool probeAHealthy {false};
  bool probeBHealthy {false};
  bool feedForwardEnabled {false};
  bool tempPlausible {true};
  bool probeAPlausible {true};
  bool probeBPlausible {true};
  bool heatingEnabled {false};
  bool stageTimerStarted {false};
  bool pendingProfileCompletePublish {false};
  bool heatOn {false};
  bool pairingWindowActive {false};
  bool pairedMetadataPresent {false};
  bool controllerEnrollmentPending {false};
  bool controllerConnected {false};
  bool integratedFallbackActive {false};
  bool controlEnabled {true};
  bool lowTempAlarmActive {false};
  bool highTempAlarmActive {false};
  DeviceType deviceType {DeviceType::ThermalController};
  uint8_t probeCount {0};
  uint8_t settingsItemIndex {0};
  char sensorMode[8] {"single"};
  uint32_t lastValidMqttConnectionAtMs {0};
  uint32_t lastAcceptedRemoteCommandAtMs {0};
  uint32_t pairingWindowEndsAtMs {0};
  uint32_t lastControllerSupervisionAtMs {0};
  char systemId[24] {"unbound"};
  char systemName[32] {""};
  char controllerId[24] {""};

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
  bool alarmAcknowledged {false};
  char alarmText[64] {"OK"};
  char settingsLabel[24] {""};
  char settingsValue[48] {""};
  RuntimeEvent eventLog[CoreConfig::EVENT_LOG_CAPACITY];
  uint8_t eventLogHead {0};
  uint8_t eventLogCount {0};
  bool pendingEventLogPublish {false};
};
