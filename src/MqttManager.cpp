#include "MqttManager.h"
#include "Config.h"
#include "DebugControl.h"
#include <ArduinoJson.h>
#include <functional>

MqttManager::MqttManager() : _client(_wifiClient) {}
namespace {
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
  _transportClient = &_wifiClient;
  _client.setBufferSize(1024);
  configureClientForSecurity(true);
  _client.setServer(cfg->mqttHost, effectivePort());
  _client.setCallback([this](char* topic, byte* payload, unsigned int length) {
    this->handleMessage(topic, payload, length);
  });
}

void MqttManager::setCommandCallback(CommandCallback cb) {
  _commandCallback = cb;
}

void MqttManager::update() {
  if (!_cfg || !_rt) return;

  configureClientForSecurity();
  _client.setServer(_cfg->mqttHost, effectivePort());

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
  if (millis() - _lastReconnectMs < Config::MQTT_RECONNECT_MS) return;
  _lastReconnectMs = millis();

  bool ok = false;
  if (strlen(_cfg->mqttUser) > 0) {
    ok = _client.connect(Config::MQTT_CLIENT_ID, _cfg->mqttUser, _cfg->mqttPass);
  } else {
    ok = _client.connect(Config::MQTT_CLIENT_ID);
  }

  if (ok) {
    DBG_LOGF("MQTT connected (%s:%u tls=%d)\n", _cfg->mqttHost, effectivePort(), _cfg->mqttUseTls);
    subscribeTopics();
    _rt->lastValidMqttConnectionAtMs = millis();
  } else {
    DBG_LOGLN("MQTT reconnect failed");
  }
}

void MqttManager::subscribeTopics() {
  String topic = String(Config::MQTT_TOPIC_BASE) + "/cmd/#";
  _client.subscribe(topic.c_str());
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
  doc["wifiConnected"] = rt.wifiConnected;
  doc["mqttConnected"] = rt.mqttConnected;
  doc["debugEnabled"] = gDebugEnabled;
  doc["runtimeNetworkToggles"] = debugRuntimeNetworkTogglesEnabled();
  doc["wifiDisabledEffective"] = debugWifiDisabledEffective();
  doc["mqttDisabledEffective"] = debugMqttDisabledEffective();
  doc["networkMode"] = debugNetworkModeLabel();
  doc["alarmCode"] = static_cast<uint8_t>(rt.activeAlarm);
  doc["alarmText"] = rt.alarmText;
  doc["activeStage"] = activeStageName ? activeStageName : "";
  doc["remainingSec"] = remainingSec;

  String out;
  serializeJson(doc, out);

  String topic = String(Config::MQTT_TOPIC_BASE) + "/status";
  _client.publish(topic.c_str(), out.c_str(), true);
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

  String out;
  serializeJson(doc, out);

  String topic = String(Config::MQTT_TOPIC_BASE) + "/shadow";
  _client.publish(topic.c_str(), out.c_str(), true);
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

  String out;
  serializeJson(doc, out);

  String topic = String(Config::MQTT_TOPIC_BASE) + "/event/cmd_ack";
  _client.publish(topic.c_str(), out.c_str(), false);
}

void MqttManager::publishCalibrationStatus(const PersistentConfig& cfg, const RuntimeState& rt) {
  if (!_client.connected()) return;

  JsonDocument doc;
  doc["tempOffsetC"] = cfg.tempOffsetC;
  doc["tempSmoothingAlpha"] = cfg.tempSmoothingAlpha;
  doc["tempC"] = rt.currentTempC;
  doc["rawTempC"] = rt.currentRawTempC;
  doc["sensorHealthy"] = rt.sensorHealthy;
  doc["tempPlausible"] = rt.tempPlausible;

  String out;
  serializeJson(doc, out);

  String topic = String(Config::MQTT_TOPIC_BASE) + "/status/calibration";
  _client.publish(topic.c_str(), out.c_str(), true);
}

void MqttManager::publishProfileCompleteIfPending(RuntimeState& rt) {
  if (!_client.connected() || !rt.pendingProfileCompletePublish) return;
  String topic = String(Config::MQTT_TOPIC_BASE) + "/event/profile_complete";
  _client.publish(topic.c_str(), "true", true);
  rt.pendingProfileCompletePublish = false;
}

void MqttManager::publishConfig(const PersistentConfig& cfg, const RuntimeState& rt) {
  if (!_client.connected()) return;

  JsonDocument doc;
  doc["controlLock"] = static_cast<uint8_t>(cfg.controlLock);
  doc["localSetpointC"] = cfg.localSetpointC;
  doc["manualStageMinutes"] = cfg.manualStageMinutes;
  doc["overTempC"] = cfg.overTempC;
  doc["mqttHost"] = cfg.mqttHost;
  doc["mqttPort"] = cfg.mqttPort;
  doc["pidKp"] = cfg.pidKp;
  doc["pidKi"] = cfg.pidKi;
  doc["pidKd"] = cfg.pidKd;
  doc["activePidKp"] = rt.currentKp;
  doc["activePidKi"] = rt.currentKi;
  doc["activePidKd"] = rt.currentKd;
  doc["autoTuneQualityScore"] = cfg.tuneQualityScore;

  String out;
  serializeJson(doc, out);

  String topic = String(Config::MQTT_TOPIC_BASE) + "/config/effective";
  _client.publish(topic.c_str(), out.c_str(), true);
}

bool MqttManager::isConnected() {
  return _client.connected();
}

uint16_t MqttManager::effectivePort() const {
  if (!_cfg) return Config::MQTT_PORT_PLAIN;
  if (_cfg->mqttUseTls && _cfg->mqttPort == Config::MQTT_PORT_PLAIN) return Config::MQTT_PORT_TLS;
  return _cfg->mqttPort;
}

void MqttManager::configureClientForSecurity(bool force) {
  if (!_cfg) return;

  const bool tlsEnabled = _cfg->mqttUseTls;
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
