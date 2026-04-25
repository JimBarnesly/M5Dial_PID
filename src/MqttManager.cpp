#include "MqttManager.h"
#include "core/CoreConfig.h"
#include "DebugControl.h"
#include "TestingMode.h"
#include <ArduinoJson.h>
#include <esp_system.h>
#include <functional>

MqttManager::MqttManager() : _client(_wifiClient) {}
namespace {
constexpr char kMqttCommandTopic[] = "/cmd";
constexpr char kMqttStatusTopic[] = "/status";
constexpr char kMqttShadowTopic[] = "/shadow";
constexpr char kMqttConfigTopic[] = "/config";
constexpr char kMqttEventsTopic[] = "/events";
constexpr char kMqttCalibrationTopic[] = "/calibration";
constexpr char kMqttLifecycleTopic[] = "/lifecycle";
constexpr char kMqttEnrollmentRequestTopic[] = "/enrollment/request";
constexpr char kMqttEnrollmentResponseTopic[] = "/enrollment/response";
constexpr char kMqttControllerHeartbeatTopic[] = "/controller/heartbeat";
constexpr uint16_t kMqttClientBufferSize = 4096;

void logPublishFailure(const char* topicLeaf, size_t payloadLen) {
  Serial.printf("[MQTT] publish failed topic=%s payload_len=%u buffer=%u\n",
                topicLeaf ? topicLeaf : "<null>",
                static_cast<unsigned>(payloadLen),
                static_cast<unsigned>(kMqttClientBufferSize));
}

const char* runStateText(RunState runState) {
  switch (runState) {
    case RunState::Idle: return "idle";
    case RunState::Running: return "running";
    case RunState::Paused: return "paused";
    case RunState::Complete: return "complete";
    case RunState::Fault: return "fault";
    case RunState::AutoTune: return "autotune";
    default: return "unknown";
  }
}
}

void MqttManager::begin(PersistentConfig* cfg, RuntimeState* rt) {
  _cfg = cfg;
  _rt = rt;
  buildClientId();
  _transportClient = &_wifiClient;
  _client.setBufferSize(kMqttClientBufferSize);
  configureClientForSecurity(true);
  applyBrokerEndpoint();
  _client.setCallback([this](char* topic, byte* payload, unsigned int length) {
    this->handleMessage(topic, payload, length);
  });
}

void MqttManager::buildClientId() {
  if (_cfg && _cfg->deviceId[0] != '\0') {
    snprintf(_clientId, sizeof(_clientId), "%s_%s", CoreConfig::MQTT_CLIENT_ID, _cfg->deviceId);
    return;
  }

  const uint64_t efuseMac = ESP.getEfuseMac();
  const uint32_t suffix = static_cast<uint32_t>(efuseMac & 0xFFFFFFULL);
  snprintf(_clientId, sizeof(_clientId), "%s_%06lX", CoreConfig::MQTT_CLIENT_ID, static_cast<unsigned long>(suffix));
}

void MqttManager::setCommandCallback(CommandCallback cb) {
  _commandCallback = cb;
}

const char* MqttManager::clientId() const {
  return _clientId;
}

String MqttManager::topicBase() const {
  if (!_cfg) return String("/systems/") + CoreConfig::MQTT_DEFAULT_SYSTEM_ID + "/devices/" +
                           CoreConfig::MQTT_DEVICE_CLASS + "/unknown";
  const char* systemId = (_rt && _rt->systemId[0] != '\0') ? _rt->systemId : _cfg->systemId;
  return String("/systems/") + systemId + "/devices/" + CoreConfig::MQTT_DEVICE_CLASS + "/" + _cfg->deviceId;
}

void MqttManager::update() {
  if (!_cfg || !_rt) return;

  configureClientForSecurity();
  applyBrokerEndpoint();

  if (!_rt->wifiConnected) {
    _rt->mqttConnected = false;
    return;
  }

  if (_client.connected()) {
    // MQTT loop servicing
    _client.loop();
    _rt->mqttConnected = true;
    _rt->lastValidMqttConnectionAtMs = millis();
    return;
  }

  tryReconnect();
  _rt->mqttConnected = _client.connected();
}

void MqttManager::tryReconnect() {
  if (millis() - _lastReconnectMs < CoreConfig::MQTT_RECONNECT_MS) return;
  _lastReconnectMs = millis();

  bool ok = false;
  if (TestingMode::mqttUseAuth(*_cfg)) {
    ok = _client.connect(_clientId, _cfg->mqttUser, _cfg->mqttPass);
  } else {
    ok = _client.connect(_clientId);
  }

  if (ok) {
    const char* host = currentBrokerHost();
    DBG_LOGF("MQTT connected host=%s port=%u tls=%d test=%d auth=%d clientId=%s literal_ip=%d\n",
             host,
             effectivePort(),
             TestingMode::mqttTlsEnabled(*_cfg),
             TestingMode::enabled(*_cfg),
             TestingMode::mqttUseAuth(*_cfg),
             _clientId,
             usingLiteralBrokerIp());
    Serial.printf("[MQTT] connected host=%s port=%u tls=%d test=%d auth=%d clientId=%s literal_ip=%d\n",
                  host,
                  effectivePort(),
                  TestingMode::mqttTlsEnabled(*_cfg),
                  TestingMode::enabled(*_cfg),
                  TestingMode::mqttUseAuth(*_cfg),
                  _clientId,
                  usingLiteralBrokerIp());
    subscribeTopics();
    _rt->lastValidMqttConnectionAtMs = millis();
  } else {
    const char* host = currentBrokerHost();
    DBG_LOGF("MQTT reconnect failed state=%d host=%s port=%u tls=%d wifi=%d auth=%d clientId=%s literal_ip=%d\n",
             _client.state(),
             host,
             effectivePort(),
             TestingMode::mqttTlsEnabled(*_cfg),
             _rt ? _rt->wifiConnected : false,
             TestingMode::mqttUseAuth(*_cfg),
             _clientId,
             usingLiteralBrokerIp());
    Serial.printf("[MQTT] reconnect failed state=%d host=%s port=%u tls=%d wifi=%d auth=%d clientId=%s literal_ip=%d\n",
                  _client.state(),
                  host,
                  effectivePort(),
                  TestingMode::mqttTlsEnabled(*_cfg),
                  _rt ? _rt->wifiConnected : false,
                  TestingMode::mqttUseAuth(*_cfg),
                  _clientId,
                  usingLiteralBrokerIp());
  }
}

void MqttManager::subscribeTopics() {
  String commandTopic = topicFor(kMqttCommandTopic);
  _client.subscribe(commandTopic.c_str());

  // Backward compatibility: continue accepting direct per-command topics.
  String legacyTopic = topicFor("/cmd/+");
  _client.subscribe(legacyTopic.c_str());
  _client.subscribe(topicFor(kMqttEnrollmentResponseTopic).c_str());
  _client.subscribe(topicFor(kMqttControllerHeartbeatTopic).c_str());
}

String MqttManager::topicFor(const char* leaf) const {
  return topicBase() + (leaf ? leaf : "");
}

void MqttManager::handleMessage(char* topic, byte* payload, unsigned int length) {
  String data;
  for (unsigned int i = 0; i < length; ++i) data += static_cast<char>(payload[i]);
  if (_commandCallback) {
    _commandCallback(topic, data.c_str());
  }
}

void MqttManager::publishStatus(const RuntimeState& rt, const char* activeStageName, uint32_t remainingSec) {
  if (!_client.connected()) return;

  JsonDocument doc;
  doc["tempC"] = rt.currentTempC;
  doc["rawTempC"] = rt.currentRawTempC;
  doc["sensor_mode"] = rt.sensorMode;
  doc["probe_count"] = rt.probeCount;
  doc["probe_a_temp"] = rt.probeATempC;
  if (rt.probeCount >= 2 && !isnan(rt.probeBTempC)) doc["probe_b_temp"] = rt.probeBTempC;
  else doc["probe_b_temp"] = nullptr;
  doc["probe_a_ok"] = rt.probeAHealthy;
  doc["probe_b_ok"] = rt.probeBHealthy;
  doc["feed_forward_enabled"] = rt.feedForwardEnabled;
  doc["operating_mode"] = (rt.operatingMode == OperatingMode::Integrated) ? "integrated" : "standalone";
  doc["integration_state"] = static_cast<uint8_t>(rt.integrationState);
  doc["paired_metadata_present"] = rt.pairedMetadataPresent;
  doc["controller_enrollment_pending"] = rt.controllerEnrollmentPending;
  doc["controller_connected"] = rt.controllerConnected;
  doc["integrated_fallback_active"] = rt.integratedFallbackActive;
  doc["testingModeEnabled"] = rt.testingModeActive;
  doc["control_authority"] =
      (rt.controlAuthority == ControlAuthority::Controller) ? "controller"
      : (rt.controlAuthority == ControlAuthority::LocalOverride) ? "local_override"
      : "local";
  doc["control_enabled"] = rt.controlEnabled;
  doc["system_id"] = rt.systemId;
  doc["system_name"] = rt.systemName;
  doc["controller_id"] = rt.controllerId;
  doc["setpointC"] = rt.currentSetpointC;
  doc["heaterOutputPct"] = rt.heaterOutputPct;
  doc["heaterEnabled"] = rt.heatingEnabled;
  doc["controlMode"] = static_cast<uint8_t>(rt.controlMode);
  doc["runState"] = static_cast<uint8_t>(rt.runState);
  doc["autoTunePhase"] = static_cast<uint8_t>(rt.autoTunePhase);
  doc["autoTuneRiseTimeSec"] = rt.autoTuneRiseTimeSec;
  doc["autoTuneOvershootC"] = rt.autoTuneOvershootC;
  doc["autoTuneSettlingSec"] = rt.autoTuneSettlingSec;
  doc["autoTuneQualityScore"] = rt.autoTuneQualityScore;
  doc["pidKp"] = rt.currentKp;
  doc["pidKi"] = rt.currentKi;
  doc["pidKd"] = rt.currentKd;
  doc["prevPidKp"] = rt.previousKp;
  doc["prevPidKi"] = rt.previousKi;
  doc["prevPidKd"] = rt.previousKd;
  doc["sensorHealthy"] = rt.sensorHealthy;
  doc["tempPlausible"] = rt.tempPlausible;
  doc["low_temp_alarm_active"] = rt.lowTempAlarmActive;
  doc["high_temp_alarm_active"] = rt.highTempAlarmActive;
  doc["wifiConnected"] = rt.wifiConnected;
  doc["mqttConnected"] = rt.mqttConnected;
  doc["debugEnabled"] = gDebugEnabled;
  doc["runtimeNetworkToggles"] = debugRuntimeNetworkTogglesEnabled();
  doc["wifiDisabledEffective"] = debugWifiDisabledEffective();
  doc["mqttDisabledEffective"] = debugMqttDisabledEffective();
  doc["networkMode"] = debugNetworkModeLabel();
  doc["alarmCode"] = static_cast<uint8_t>(rt.activeAlarm);
  doc["alarmAcknowledged"] = rt.alarmAcknowledged;
  doc["alarmText"] = rt.alarmText;
  doc["activeStage"] = activeStageName ? activeStageName : "";
  doc["remainingSec"] = remainingSec;
  doc["_type"] = "status";

  String out;
  serializeJson(doc, out);

  String topic = topicFor(kMqttStatusTopic);
  if (!_client.publish(topic.c_str(), out.c_str(), true)) {
    logPublishFailure(kMqttStatusTopic, out.length());
  }
  publishShadow(rt, remainingSec);
}

void MqttManager::publishShadow(const RuntimeState& rt, uint32_t remainingSec) {
  if (!_client.connected()) return;

  JsonDocument doc;
  JsonObject desired = doc["desired"].to<JsonObject>();
  desired["setpointC"] = rt.desiredSetpointC;
  desired["minutes"] = rt.desiredMinutes;
  desired["runAction"] = rt.desiredRunAction;

  JsonObject reported = doc["reported"].to<JsonObject>();
  reported["runState"] = runStateText(rt.runState);
  reported["setpointC"] = rt.currentSetpointC;
  reported["effectiveTimerSec"] = remainingSec;
  reported["stageTimerStarted"] = rt.stageTimerStarted;
  doc["_type"] = "shadow";

  String out;
  serializeJson(doc, out);

  String topic = topicFor(kMqttShadowTopic);
  if (!_client.publish(topic.c_str(), out.c_str(), true)) {
    logPublishFailure(kMqttShadowTopic, out.length());
  }
}

void MqttManager::publishCommandAck(const char* cmdId,
                                    const char* command,
                                    bool accepted,
                                    bool applied,
                                    const char* reason,
                                    const RuntimeState& rt,
                                    uint32_t remainingSec) {
  if (!_client.connected()) return;

  JsonDocument doc;
  doc["cmdId"] = (cmdId && cmdId[0] != '\0') ? cmdId : "none";
  doc["command"] = command ? command : "";
  doc["accepted"] = accepted;
  doc["applied"] = applied;
  doc["reason"] = reason ? reason : "";

  JsonObject reported = doc["reported"].to<JsonObject>();
  reported["runState"] = runStateText(rt.runState);
  reported["setpointC"] = rt.currentSetpointC;
  reported["effectiveTimerSec"] = remainingSec;
  doc["_type"] = "cmd_ack";

  String out;
  serializeJson(doc, out);

  String topic = topicFor(kMqttConfigTopic);
  _client.publish(topic.c_str(), out.c_str(), false);
}

void MqttManager::publishCalibrationStatus(const PersistentConfig& cfg, const RuntimeState& rt) {
  if (!_client.connected()) return;

  JsonDocument doc;
  doc["tempOffsetC"] = cfg.tempOffsetC;
  doc["tempSmoothingAlpha"] = cfg.tempSmoothingAlpha;
  doc["tempC"] = rt.currentTempC;
  doc["rawTempC"] = rt.currentRawTempC;
  doc["sensor_mode"] = rt.sensorMode;
  doc["probe_count"] = rt.probeCount;
  doc["probe_a_temp"] = rt.probeATempC;
  if (rt.probeCount >= 2 && !isnan(rt.probeBTempC)) doc["probe_b_temp"] = rt.probeBTempC;
  else doc["probe_b_temp"] = nullptr;
  doc["probe_a_ok"] = rt.probeAHealthy;
  doc["probe_b_ok"] = rt.probeBHealthy;
  doc["feed_forward_enabled"] = rt.feedForwardEnabled;
  doc["sensorHealthy"] = rt.sensorHealthy;
  doc["tempPlausible"] = rt.tempPlausible;
  doc["_type"] = "calibration_status";

  String out;
  serializeJson(doc, out);

  String topic = topicFor(kMqttCalibrationTopic);
  if (!_client.publish(topic.c_str(), out.c_str(), true)) {
    logPublishFailure(kMqttCalibrationTopic, out.length());
  }
}

void MqttManager::publishProfileCompleteIfPending(RuntimeState& rt) {
  if (!_client.connected() || !rt.pendingProfileCompletePublish) return;
  JsonDocument doc;
  doc["_type"] = "profile_complete";
  doc["value"] = true;
  String out;
  serializeJson(doc, out);
  String topic = topicFor(kMqttLifecycleTopic);
  if (!_client.publish(topic.c_str(), out.c_str(), true)) {
    logPublishFailure(kMqttLifecycleTopic, out.length());
  }
  rt.pendingProfileCompletePublish = false;
}

void MqttManager::publishConfig(const PersistentConfig& cfg, const RuntimeState& rt) {
  if (!_client.connected()) return;

  JsonDocument doc;
  doc["controlLock"] = static_cast<uint8_t>(cfg.controlLock);
  doc["systemId"] = rt.systemId;
  doc["deviceId"] = cfg.deviceId;
  doc["deviceClass"] = CoreConfig::MQTT_DEVICE_CLASS;
  doc["operatingMode"] = (rt.operatingMode == OperatingMode::Integrated) ? "integrated" : "standalone";
  doc["integrationState"] = static_cast<uint8_t>(rt.integrationState);
  doc["pairedMetadataPresent"] = rt.pairedMetadataPresent;
  doc["controllerEnrollmentPending"] = rt.controllerEnrollmentPending;
  doc["controllerConnected"] = rt.controllerConnected;
  doc["integratedFallbackActive"] = rt.integratedFallbackActive;
  doc["controlAuthority"] =
      (rt.controlAuthority == ControlAuthority::Controller) ? "controller"
      : (rt.controlAuthority == ControlAuthority::LocalOverride) ? "local_override"
      : "local";
  doc["controlEnabled"] = cfg.controlEnabled;
  doc["testingModeEnabled"] = rt.testingModeActive;
  doc["localAuthorityOverride"] = cfg.localAuthorityOverride;
  doc["systemName"] = rt.systemName;
  doc["controllerId"] = rt.controllerId;
  doc["localSetpointC"] = cfg.localSetpointC;
  doc["minSetpointC"] = cfg.minSetpointC;
  doc["maxSetpointC"] = cfg.maxSetpointC;
  doc["manualStageMinutes"] = cfg.manualStageMinutes;
  doc["overTempC"] = cfg.overTempC;
  doc["tempAlarmEnabled"] = cfg.tempAlarmEnabled;
  doc["lowAlarmC"] = cfg.lowAlarmC;
  doc["highAlarmC"] = cfg.highAlarmC;
  doc["alarmHysteresisC"] = cfg.alarmHysteresisC;
  doc["alarmEnableSensorFault"] = cfg.alarmEnableSensorFault;
  doc["alarmEnableOverTemp"] = cfg.alarmEnableOverTemp;
  doc["alarmEnableHeatingIneffective"] = cfg.alarmEnableHeatingIneffective;
  doc["alarmEnableMqttOffline"] = cfg.alarmEnableMqttOffline;
  doc["alarmEnableLowProcessTemp"] = cfg.alarmEnableLowProcessTemp;
  doc["alarmEnableHighProcessTemp"] = cfg.alarmEnableHighProcessTemp;
  doc["mqttHost"] = TestingMode::mqttHost(cfg);
  doc["mqttPort"] = effectivePort();
  doc["mqttUseTls"] = TestingMode::mqttTlsEnabled(cfg);
  doc["mqttCommsTimeoutSec"] = cfg.mqttCommsTimeoutSec;
  doc["mqttFallbackMode"] = static_cast<uint8_t>(cfg.mqttFallbackMode);
  doc["wifiPortalTimeoutSec"] = cfg.wifiPortalTimeoutSec;
  doc["profileCount"] = cfg.profileCount;
  doc["activeProfileIndex"] = cfg.activeProfileIndex;
  doc["pidKp"] = cfg.pidKp;
  doc["pidKi"] = cfg.pidKi;
  doc["pidKd"] = cfg.pidKd;
  doc["pidDirection"] = static_cast<uint8_t>(cfg.pidDirection);
  doc["maxOutputPercent"] = cfg.maxOutputPercent;
  doc["activePidKp"] = rt.currentKp;
  doc["activePidKi"] = rt.currentKi;
  doc["activePidKd"] = rt.currentKd;
  doc["autoTuneQualityScore"] = cfg.tuneQualityScore;
  doc["displayBrightness"] = cfg.displayBrightness;
  doc["buzzerEnabled"] = cfg.buzzerEnabled;
  doc["_type"] = "config_effective";

  String out;
  serializeJson(doc, out);

  String topic = topicFor(kMqttConfigTopic);
  if (!_client.publish(topic.c_str(), out.c_str(), true)) {
    logPublishFailure(kMqttConfigTopic, out.length());
  }
}

void MqttManager::publishEventLog(const RuntimeState& rt) {
  if (!_client.connected()) return;

  JsonDocument doc;
  JsonArray events = doc["events"].to<JsonArray>();
  const uint8_t count = rt.eventLogCount > CoreConfig::EVENT_LOG_CAPACITY ? CoreConfig::EVENT_LOG_CAPACITY : rt.eventLogCount;
  const uint8_t start = (rt.eventLogHead + CoreConfig::EVENT_LOG_CAPACITY - count) % CoreConfig::EVENT_LOG_CAPACITY;
  for (uint8_t i = 0; i < count; ++i) {
    const RuntimeEvent& ev = rt.eventLog[(start + i) % CoreConfig::EVENT_LOG_CAPACITY];
    JsonObject item = events.add<JsonObject>();
    item["atMs"] = ev.atMs;
    item["text"] = ev.text;
  }
  doc["count"] = count;
  doc["_type"] = "event_log";

  String out;
  serializeJson(doc, out);
  String topic = topicFor(kMqttEventsTopic);
  if (!_client.publish(topic.c_str(), out.c_str(), false)) {
    logPublishFailure(kMqttEventsTopic, out.length());
  }
}

bool MqttManager::isConnected() {
  return _client.connected();
}

bool MqttManager::publishRaw(const char* leaf, const char* payload, bool retained) {
  if (!_client.connected() || !payload) return false;
  const String topic = topicFor(leaf);
  return _client.publish(topic.c_str(), payload, retained);
}

void MqttManager::setBrokerOverride(const char* host, uint16_t port) {
  strlcpy(_brokerOverrideHost, host ? host : "", sizeof(_brokerOverrideHost));
  _brokerOverridePort = port;
}

void MqttManager::clearBrokerOverride() {
  _brokerOverrideHost[0] = '\0';
  _brokerOverridePort = 0;
}

void MqttManager::applyBrokerEndpoint() {
  if (!_cfg) return;
  if (usingLiteralBrokerIp()) {
    _client.setServer(TestingMode::mqttIp(), effectivePort());
  } else {
    _client.setServer(currentBrokerHost(), effectivePort());
  }
}

const char* MqttManager::currentBrokerHost() const {
  if (!_cfg) return TestingMode::MQTT_HOST;
  if (TestingMode::enabled(*_cfg)) return TestingMode::MQTT_HOST;
  return _brokerOverrideHost[0] != '\0' ? _brokerOverrideHost : _cfg->mqttHost;
}

bool MqttManager::usingLiteralBrokerIp() const {
  return _cfg && TestingMode::enabled(*_cfg);
}

uint16_t MqttManager::effectivePort() const {
  if (!_cfg) return CoreConfig::MQTT_PORT_PLAIN;
  if (TestingMode::enabled(*_cfg)) return TestingMode::MQTT_PORT;
  uint16_t port = _brokerOverridePort != 0 ? _brokerOverridePort : _cfg->mqttPort;
  if (TestingMode::mqttTlsEnabled(*_cfg) && port == CoreConfig::MQTT_PORT_PLAIN) return CoreConfig::MQTT_PORT_TLS;
  return port;
}

void MqttManager::configureClientForSecurity(bool force) {
  if (!_cfg) return;

  const bool tlsEnabled = TestingMode::mqttTlsEnabled(*_cfg);
  const uint16_t desiredPort = effectivePort();
  if (!force && tlsEnabled == _lastTlsEnabled && desiredPort == _lastPort) return;

  if (tlsEnabled) {
    _transportClient = &_wifiSecureClient;
    _wifiSecureClient.setHandshakeTimeout(15);
    if (_cfg->mqttTlsAuthMode == 1 && strlen(_cfg->mqttTlsFingerprint) > 0) {
      // Fingerprint pinning API is not available in this ESP32 core version.
      // Fall back to CA pinning (when provided) or insecure mode.
      if (strlen(_cfg->mqttTlsCaCert) > 0) _wifiSecureClient.setCACert(_cfg->mqttTlsCaCert);
      else _wifiSecureClient.setInsecure();
    } else if (_cfg->mqttTlsAuthMode == 2 && strlen(_cfg->mqttTlsCaCert) > 0) {
      _wifiSecureClient.setCACert(_cfg->mqttTlsCaCert);
    } else {
      _wifiSecureClient.setInsecure();
    }
  } else {
    _transportClient = &_wifiClient;
  }

  _client.setClient(*_transportClient);
  _lastTlsEnabled = tlsEnabled;
  _lastPort = desiredPort;
}
