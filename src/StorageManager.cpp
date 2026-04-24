#include "StorageManager.h"
#include <ArduinoJson.h>
#include <cstring>
#include <esp_system.h>
#include <esp_flash_encrypt.h>

namespace {
constexpr char kIntegrationModeKey[] = "mode";
constexpr char kIntegrationPairedKey[] = "paired";
constexpr char kIntegrationStateKey[] = "int_state";
constexpr char kIntegrationDeviceTypeKey[] = "dev_type";
constexpr char kIntegrationSchemaKey[] = "int_ver";
constexpr char kIntegrationSystemIdKey[] = "system_id";
constexpr char kIntegrationSystemNameKey[] = "system_name";
constexpr char kIntegrationControllerIdKey[] = "controller_id";
constexpr char kIntegrationControllerPubKey[] = "controller_pub";
constexpr char kIntegrationControllerFingerprintKey[] = "controller_fp";
constexpr char kIntegrationApSsidKey[] = "ap_ssid";
constexpr char kIntegrationApPskKey[] = "ap_psk";
constexpr char kIntegrationBrokerHostKey[] = "broker_host";
constexpr char kIntegrationBrokerPortKey[] = "broker_port";
constexpr char kIntegrationIssuedAtKey[] = "issued_at";
constexpr char kIntegrationEpochKey[] = "epoch";

void buildDefaultDeviceId(char* out, size_t outSize) {
  const uint64_t efuseMac = ESP.getEfuseMac();
  const uint32_t suffix = static_cast<uint32_t>(efuseMac & 0xFFFFFFULL);
  snprintf(out, outSize, "%s-%06lX", CoreConfig::MQTT_DEVICE_CLASS, static_cast<unsigned long>(suffix));
}
}

bool StorageManager::encryptedStorageAvailable() const {
  return esp_flash_encryption_enabled();
}

void StorageManager::begin() {
  _prefs.begin("env-ctrl", false);
  _lastSavedJson = _prefs.getString("cfg", "");
}

void StorageManager::loadDefaults(PersistentConfig& cfg) {
  memset(&cfg, 0, sizeof(cfg));
  strlcpy(cfg.systemId, CoreConfig::MQTT_DEFAULT_SYSTEM_ID, sizeof(cfg.systemId));
  buildDefaultDeviceId(cfg.deviceId, sizeof(cfg.deviceId));
  cfg.controlLock = ControlLock::LocalOrRemote;
  cfg.controlEnabled = true;
  cfg.localAuthorityOverride = false;
  cfg.localSetpointC = 65.0f;
  cfg.minSetpointC = CoreConfig::DEFAULT_MIN_SETPOINT_C;
  cfg.maxSetpointC = CoreConfig::DEFAULT_MAX_SETPOINT_C;
  cfg.stageStartBandC = 2.0f;
  cfg.manualStageMinutes = 60;
  cfg.overTempC = 99.0f;
  cfg.tempAlarmEnabled = true;
  cfg.lowAlarmC = CoreConfig::DEFAULT_LOW_ALARM_C;
  cfg.highAlarmC = CoreConfig::DEFAULT_HIGH_ALARM_C;
  cfg.alarmHysteresisC = CoreConfig::DEFAULT_ALARM_HYSTERESIS_C;
  cfg.tempOffsetC = 0.0f;
  cfg.tempSmoothingAlpha = CoreConfig::DEFAULT_TEMP_SMOOTHING_ALPHA;
  strncpy(cfg.mqttHost, "10.42.0.1", sizeof(cfg.mqttHost)-1);
  cfg.mqttPort = CoreConfig::MQTT_PORT_TLS;
  cfg.mqttUseTls = true;
  cfg.mqttTlsAuthMode = 0;
  cfg.mqttTlsFingerprint[0] = '\0';
  cfg.mqttTlsCaCert[0] = '\0';
  cfg.mqttCommsTimeoutSec = 0;
  cfg.mqttFallbackMode = MqttFallbackMode::HoldSetpoint;
  cfg.wifiPortalTimeoutSec = CoreConfig::DEFAULT_WIFI_PORTAL_TIMEOUT_SEC;
  cfg.pidKp = CoreConfig::PID_KP;
  cfg.pidKi = CoreConfig::PID_KI;
  cfg.pidKd = CoreConfig::PID_KD;
  cfg.pidDirection = PidDirection::Direct;
  cfg.maxOutputPercent = 100.0f;
  cfg.prevPidKp = cfg.pidKp;
  cfg.prevPidKi = cfg.pidKi;
  cfg.prevPidKd = cfg.pidKd;
  cfg.tuneQualityScore = 0.0f;
  cfg.displayBrightness = 128;
  cfg.buzzerEnabled = true;
  cfg.profileCount = 0;
  cfg.activeProfileIndex = 0;
}

bool StorageManager::load(PersistentConfig& cfg) {
  String json = _prefs.getString("cfg", "");
  _lastSavedJson = json;
  if (json.isEmpty()) {
    loadDefaults(cfg);
    save(cfg);
    return true;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    loadDefaults(cfg);
    save(cfg);
    return false;
  }

  loadDefaults(cfg);
  cfg.localSetpointC = doc["localSetpointC"] | cfg.localSetpointC;
  cfg.controlEnabled = doc["controlEnabled"] | cfg.controlEnabled;
  cfg.localAuthorityOverride = doc["localAuthorityOverride"] | cfg.localAuthorityOverride;
  cfg.minSetpointC = doc["minSetpointC"] | cfg.minSetpointC;
  cfg.maxSetpointC = doc["maxSetpointC"] | cfg.maxSetpointC;
  cfg.stageStartBandC = doc["stageStartBandC"] | cfg.stageStartBandC;
  cfg.manualStageMinutes = doc["manualStageMinutes"] | cfg.manualStageMinutes;
  cfg.overTempC = doc["overTempC"] | cfg.overTempC;
  cfg.tempAlarmEnabled = doc["tempAlarmEnabled"] | cfg.tempAlarmEnabled;
  cfg.lowAlarmC = doc["lowAlarmC"] | cfg.lowAlarmC;
  cfg.highAlarmC = doc["highAlarmC"] | cfg.highAlarmC;
  cfg.alarmHysteresisC = doc["alarmHysteresisC"] | cfg.alarmHysteresisC;
  cfg.tempOffsetC = doc["tempOffsetC"] | cfg.tempOffsetC;
  cfg.tempSmoothingAlpha = doc["tempSmoothingAlpha"] | cfg.tempSmoothingAlpha;
  cfg.controlLock = static_cast<ControlLock>((uint8_t)(doc["controlLock"] | (uint8_t)cfg.controlLock));
  strlcpy(cfg.systemId, doc["systemId"] | cfg.systemId, sizeof(cfg.systemId));
  strlcpy(cfg.deviceId, doc["deviceId"] | cfg.deviceId, sizeof(cfg.deviceId));
  const char* mqttHostDefault = "10.42.0.1";
  const bool migrated = doc["mqttHost"].isNull() || !doc["mqttHost"].is<const char*>();
  strlcpy(cfg.mqttHost, doc["mqttHost"] | mqttHostDefault, sizeof(cfg.mqttHost));
  cfg.mqttPort = doc["mqttPort"] | cfg.mqttPort;
  cfg.mqttUseTls = doc["mqttUseTls"] | cfg.mqttUseTls;
  cfg.mqttTlsAuthMode = doc["mqttTlsAuthMode"] | cfg.mqttTlsAuthMode;
  cfg.mqttCommsTimeoutSec = doc["mqttCommsTimeoutSec"] | cfg.mqttCommsTimeoutSec;
  cfg.mqttFallbackMode = static_cast<MqttFallbackMode>((uint8_t)(doc["mqttFallbackMode"] | (uint8_t)cfg.mqttFallbackMode));
  cfg.wifiPortalTimeoutSec = doc["wifiPortalTimeoutSec"] | cfg.wifiPortalTimeoutSec;
  const bool canLoadSecrets = encryptedStorageAvailable();
  if (canLoadSecrets) {
    strlcpy(cfg.mqttUser, doc["mqttUser"] | cfg.mqttUser, sizeof(cfg.mqttUser));
    strlcpy(cfg.mqttPass, doc["mqttPass"] | cfg.mqttPass, sizeof(cfg.mqttPass));
    strlcpy(cfg.mqttTlsFingerprint, doc["mqttTlsFingerprint"] | cfg.mqttTlsFingerprint, sizeof(cfg.mqttTlsFingerprint));
    strlcpy(cfg.mqttTlsCaCert, doc["mqttTlsCaCert"] | cfg.mqttTlsCaCert, sizeof(cfg.mqttTlsCaCert));
  } else {
    cfg.mqttUser[0] = '\0';
    cfg.mqttPass[0] = '\0';
    cfg.mqttTlsFingerprint[0] = '\0';
    cfg.mqttTlsCaCert[0] = '\0';
  }
  cfg.pidKp = doc["pidKp"] | cfg.pidKp;
  cfg.pidKi = doc["pidKi"] | cfg.pidKi;
  cfg.pidKd = doc["pidKd"] | cfg.pidKd;
  cfg.pidDirection = static_cast<PidDirection>((uint8_t)(doc["pidDirection"] | (uint8_t)cfg.pidDirection));
  cfg.maxOutputPercent = doc["maxOutputPercent"] | cfg.maxOutputPercent;
  cfg.prevPidKp = doc["prevPidKp"] | cfg.prevPidKp;
  cfg.prevPidKi = doc["prevPidKi"] | cfg.prevPidKi;
  cfg.prevPidKd = doc["prevPidKd"] | cfg.prevPidKd;
  cfg.tuneQualityScore = doc["tuneQualityScore"] | cfg.tuneQualityScore;
  cfg.displayBrightness = doc["displayBrightness"] | cfg.displayBrightness;
  cfg.buzzerEnabled = doc["buzzerEnabled"] | cfg.buzzerEnabled;
  cfg.profileCount = static_cast<uint8_t>(doc["profileCount"] | cfg.profileCount);
  cfg.activeProfileIndex = static_cast<uint8_t>(doc["activeProfileIndex"] | cfg.activeProfileIndex);

  JsonArray profiles = doc["profiles"].as<JsonArray>();
  if (!profiles.isNull()) {
    cfg.profileCount = 0;
    for (JsonObject p : profiles) {
      if (cfg.profileCount >= CoreConfig::MAX_PROFILES) break;
      ProcessProfile& profile = cfg.profiles[cfg.profileCount];
      strlcpy(profile.name, p["name"] | "PROFILE", sizeof(profile.name));
      profile.stageCount = static_cast<uint8_t>(p["stageCount"] | 0);
      if (profile.stageCount > CoreConfig::MAX_STAGES) profile.stageCount = CoreConfig::MAX_STAGES;

      JsonArray stages = p["stages"].as<JsonArray>();
      uint8_t loadedStages = 0;
      if (!stages.isNull()) {
        for (JsonObject s : stages) {
          if (loadedStages >= profile.stageCount || loadedStages >= CoreConfig::MAX_STAGES) break;
          ProcessStage& stage = profile.stages[loadedStages];
          strlcpy(stage.name, s["name"] | "STAGE", sizeof(stage.name));
          stage.targetC = s["targetC"] | 0.0f;
          stage.holdSeconds = s["holdSeconds"] | 0UL;
          ++loadedStages;
        }
      }
      profile.stageCount = loadedStages;
      ++cfg.profileCount;
    }
  }

  if (cfg.profileCount == 0) {
    cfg.activeProfileIndex = 0;
  } else if (cfg.activeProfileIndex >= cfg.profileCount) {
    cfg.activeProfileIndex = static_cast<uint8_t>(cfg.profileCount - 1);
  }
  if (migrated) save(cfg);
  return true;
}

void StorageManager::save(const PersistentConfig& cfg) {
  JsonDocument doc;
  doc["localSetpointC"] = cfg.localSetpointC;
  doc["controlEnabled"] = cfg.controlEnabled;
  doc["localAuthorityOverride"] = cfg.localAuthorityOverride;
  doc["systemId"] = cfg.systemId;
  doc["deviceId"] = cfg.deviceId;
  doc["minSetpointC"] = cfg.minSetpointC;
  doc["maxSetpointC"] = cfg.maxSetpointC;
  doc["stageStartBandC"] = cfg.stageStartBandC;
  doc["manualStageMinutes"] = cfg.manualStageMinutes;
  doc["overTempC"] = cfg.overTempC;
  doc["tempAlarmEnabled"] = cfg.tempAlarmEnabled;
  doc["lowAlarmC"] = cfg.lowAlarmC;
  doc["highAlarmC"] = cfg.highAlarmC;
  doc["alarmHysteresisC"] = cfg.alarmHysteresisC;
  doc["tempOffsetC"] = cfg.tempOffsetC;
  doc["tempSmoothingAlpha"] = cfg.tempSmoothingAlpha;
  doc["controlLock"] = (uint8_t)cfg.controlLock;
  doc["mqttHost"] = cfg.mqttHost;
  doc["mqttPort"] = cfg.mqttPort;
  doc["mqttUseTls"] = cfg.mqttUseTls;
  doc["mqttTlsAuthMode"] = cfg.mqttTlsAuthMode;
  doc["mqttCommsTimeoutSec"] = cfg.mqttCommsTimeoutSec;
  doc["mqttFallbackMode"] = static_cast<uint8_t>(cfg.mqttFallbackMode);
  doc["wifiPortalTimeoutSec"] = cfg.wifiPortalTimeoutSec;
  if (encryptedStorageAvailable()) {
    doc["mqttUser"] = cfg.mqttUser;
    doc["mqttPass"] = cfg.mqttPass;
    doc["mqttTlsFingerprint"] = cfg.mqttTlsFingerprint;
    doc["mqttTlsCaCert"] = cfg.mqttTlsCaCert;
  }
  doc["pidKp"] = cfg.pidKp;
  doc["pidKi"] = cfg.pidKi;
  doc["pidKd"] = cfg.pidKd;
  doc["pidDirection"] = static_cast<uint8_t>(cfg.pidDirection);
  doc["maxOutputPercent"] = cfg.maxOutputPercent;
  doc["prevPidKp"] = cfg.prevPidKp;
  doc["prevPidKi"] = cfg.prevPidKi;
  doc["prevPidKd"] = cfg.prevPidKd;
  doc["tuneQualityScore"] = cfg.tuneQualityScore;
  doc["displayBrightness"] = cfg.displayBrightness;
  doc["buzzerEnabled"] = cfg.buzzerEnabled;
  doc["profileCount"] = cfg.profileCount;
  doc["activeProfileIndex"] = cfg.activeProfileIndex;

  JsonArray profiles = doc["profiles"].to<JsonArray>();
  const uint8_t profileCount = (cfg.profileCount > CoreConfig::MAX_PROFILES) ? CoreConfig::MAX_PROFILES : cfg.profileCount;
  for (uint8_t i = 0; i < profileCount; ++i) {
    const ProcessProfile& profile = cfg.profiles[i];
    JsonObject p = profiles.add<JsonObject>();
    p["name"] = profile.name;
    const uint8_t stageCount = (profile.stageCount > CoreConfig::MAX_STAGES) ? CoreConfig::MAX_STAGES : profile.stageCount;
    p["stageCount"] = stageCount;
    JsonArray stages = p["stages"].to<JsonArray>();
    for (uint8_t j = 0; j < stageCount; ++j) {
      const ProcessStage& stage = profile.stages[j];
      JsonObject s = stages.add<JsonObject>();
      s["name"] = stage.name;
      s["targetC"] = stage.targetC;
      s["holdSeconds"] = stage.holdSeconds;
    }
  }


  String out;
  serializeJson(doc, out);
  if (out == _lastSavedJson) return;
  _prefs.putString("cfg", out);
  _lastSavedJson = out;
}

void StorageManager::loadIntegrationBinding(IntegrationBinding& binding) {
  memset(&binding, 0, sizeof(binding));
  binding.schemaVersion = 1;
  binding.operatingMode = OperatingMode::Standalone;
  binding.integrationState = IntegrationState::None;
  binding.deviceType = DeviceType::ThermalController;
  strlcpy(binding.systemId, CoreConfig::MQTT_DEFAULT_SYSTEM_ID, sizeof(binding.systemId));

  binding.schemaVersion = _prefs.getUShort(kIntegrationSchemaKey, binding.schemaVersion);
  binding.operatingMode = static_cast<OperatingMode>(_prefs.getUChar(kIntegrationModeKey, static_cast<uint8_t>(binding.operatingMode)));
  binding.paired = _prefs.getBool(kIntegrationPairedKey, false);
  binding.integrationState = static_cast<IntegrationState>(_prefs.getUChar(kIntegrationStateKey, static_cast<uint8_t>(binding.integrationState)));
  binding.deviceType = static_cast<DeviceType>(_prefs.getUChar(kIntegrationDeviceTypeKey, static_cast<uint8_t>(binding.deviceType)));
  strlcpy(binding.systemId, _prefs.getString(kIntegrationSystemIdKey, binding.systemId).c_str(), sizeof(binding.systemId));
  strlcpy(binding.systemName, _prefs.getString(kIntegrationSystemNameKey, "").c_str(), sizeof(binding.systemName));
  strlcpy(binding.controllerId, _prefs.getString(kIntegrationControllerIdKey, "").c_str(), sizeof(binding.controllerId));
  strlcpy(binding.controllerPublicKey, _prefs.getString(kIntegrationControllerPubKey, "").c_str(), sizeof(binding.controllerPublicKey));
  strlcpy(binding.controllerFingerprint, _prefs.getString(kIntegrationControllerFingerprintKey, "").c_str(), sizeof(binding.controllerFingerprint));
  strlcpy(binding.apSsid, _prefs.getString(kIntegrationApSsidKey, "").c_str(), sizeof(binding.apSsid));
  strlcpy(binding.apPsk, _prefs.getString(kIntegrationApPskKey, "").c_str(), sizeof(binding.apPsk));
  strlcpy(binding.brokerHost, _prefs.getString(kIntegrationBrokerHostKey, "").c_str(), sizeof(binding.brokerHost));
  binding.brokerPort = _prefs.getUShort(kIntegrationBrokerPortKey, 0);
  binding.issuedAt = _prefs.getUInt(kIntegrationIssuedAtKey, 0);
  binding.epoch = _prefs.getUInt(kIntegrationEpochKey, 0);
}

void StorageManager::saveIntegrationBinding(const IntegrationBinding& binding) {
  _prefs.putUShort(kIntegrationSchemaKey, binding.schemaVersion);
  _prefs.putUChar(kIntegrationModeKey, static_cast<uint8_t>(binding.operatingMode));
  _prefs.putBool(kIntegrationPairedKey, binding.paired);
  _prefs.putUChar(kIntegrationStateKey, static_cast<uint8_t>(binding.integrationState));
  _prefs.putUChar(kIntegrationDeviceTypeKey, static_cast<uint8_t>(binding.deviceType));
  _prefs.putString(kIntegrationSystemIdKey, binding.systemId);
  _prefs.putString(kIntegrationSystemNameKey, binding.systemName);
  _prefs.putString(kIntegrationControllerIdKey, binding.controllerId);
  _prefs.putString(kIntegrationControllerPubKey, binding.controllerPublicKey);
  _prefs.putString(kIntegrationControllerFingerprintKey, binding.controllerFingerprint);
  _prefs.putString(kIntegrationApSsidKey, binding.apSsid);
  _prefs.putString(kIntegrationApPskKey, binding.apPsk);
  _prefs.putString(kIntegrationBrokerHostKey, binding.brokerHost);
  _prefs.putUShort(kIntegrationBrokerPortKey, binding.brokerPort);
  _prefs.putUInt(kIntegrationIssuedAtKey, binding.issuedAt);
  _prefs.putUInt(kIntegrationEpochKey, binding.epoch);
}

void StorageManager::clearIntegrationBinding() {
  _prefs.remove(kIntegrationSchemaKey);
  _prefs.remove(kIntegrationModeKey);
  _prefs.remove(kIntegrationPairedKey);
  _prefs.remove(kIntegrationStateKey);
  _prefs.remove(kIntegrationDeviceTypeKey);
  _prefs.remove(kIntegrationSystemIdKey);
  _prefs.remove(kIntegrationSystemNameKey);
  _prefs.remove(kIntegrationControllerIdKey);
  _prefs.remove(kIntegrationControllerPubKey);
  _prefs.remove(kIntegrationControllerFingerprintKey);
  _prefs.remove(kIntegrationApSsidKey);
  _prefs.remove(kIntegrationApPskKey);
  _prefs.remove(kIntegrationBrokerHostKey);
  _prefs.remove(kIntegrationBrokerPortKey);
  _prefs.remove(kIntegrationIssuedAtKey);
  _prefs.remove(kIntegrationEpochKey);
}
