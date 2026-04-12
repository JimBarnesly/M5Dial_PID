#include "MqttManager.h"
#include "Config.h"
#include "DebugControl.h"
#include <ArduinoJson.h>
#include <functional>

MqttManager::MqttManager() : _client(_wifiClient) {}

void MqttManager::begin(PersistentConfig* cfg, RuntimeState* rt) {
  _cfg = cfg;
  _rt = rt;
  _client.setBufferSize(1024);
  _client.setServer(cfg->mqttHost, cfg->mqttPort);
  _client.setCallback([this](char* topic, byte* payload, unsigned int length) {
    this->handleMessage(topic, payload, length);
  });
}

void MqttManager::setCommandCallback(CommandCallback cb) {
  _commandCallback = cb;
}

void MqttManager::update() {
  if (!_cfg || !_rt) return;

  _client.setServer(_cfg->mqttHost, _cfg->mqttPort);

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
    DBG_LOGLN("MQTT connected");
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
  doc["wifiConnected"] = rt.wifiConnected;
  doc["mqttConnected"] = rt.mqttConnected;
  doc["lastValidMqttConnectionAtMs"] = rt.lastValidMqttConnectionAtMs;
  doc["lastAcceptedRemoteCommandAtMs"] = rt.lastAcceptedRemoteCommandAtMs;
  doc["alarmCode"] = static_cast<uint8_t>(rt.activeAlarm);
  doc["alarmText"] = rt.alarmText;
  doc["activeStage"] = activeStageName ? activeStageName : "";
  doc["remainingSec"] = remainingSec;

  String out;
  serializeJson(doc, out);

  String topic = String(Config::MQTT_TOPIC_BASE) + "/status";
  _client.publish(topic.c_str(), out.c_str(), true);
}

void MqttManager::publishProfileCompleteIfPending(RuntimeState& rt) {
  if (!_client.connected() || !rt.pendingProfileCompletePublish) return;
  String topic = String(Config::MQTT_TOPIC_BASE) + "/event/profile_complete";
  _client.publish(topic.c_str(), "true", true);
  rt.pendingProfileCompletePublish = false;
}

bool MqttManager::isConnected() {
  return _client.connected();
}
