#include "StorageManager.h"
#include <ArduinoJson.h>
#include <cstring>

void StorageManager::begin() {
  _prefs.begin("brew-hlt", false);
}

void StorageManager::loadDefaults(PersistentConfig& cfg) {
  memset(&cfg, 0, sizeof(cfg));
  cfg.controlLock = ControlLock::LocalOrRemote;
  cfg.localSetpointC = 65.0f;
  cfg.stageStartBandC = 2.0f;
  cfg.manualStageMinutes = 60;
  cfg.overTempC = 99.0f;
  strncpy(cfg.mqttHost, "192.168.1.10", sizeof(cfg.mqttHost)-1);
  cfg.mqttPort = 1883;
  cfg.pidKp = Config::PID_KP;
  cfg.pidKi = Config::PID_KI;
  cfg.pidKd = Config::PID_KD;
  cfg.prevPidKp = cfg.pidKp;
  cfg.prevPidKi = cfg.pidKi;
  cfg.prevPidKd = cfg.pidKd;
  cfg.tuneQualityScore = 0.0f;
  cfg.profileCount = 0;
  cfg.activeProfileIndex = 0;
}

bool StorageManager::load(PersistentConfig& cfg) {
  String json = _prefs.getString("cfg", "");
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
  cfg.stageStartBandC = doc["stageStartBandC"] | cfg.stageStartBandC;
  cfg.manualStageMinutes = doc["manualStageMinutes"] | cfg.manualStageMinutes;
  cfg.overTempC = doc["overTempC"] | cfg.overTempC;
  cfg.controlLock = static_cast<ControlLock>((uint8_t)(doc["controlLock"] | (uint8_t)cfg.controlLock));
  strlcpy(cfg.mqttHost, doc["mqttHost"] | cfg.mqttHost, sizeof(cfg.mqttHost));
  cfg.mqttPort = doc["mqttPort"] | cfg.mqttPort;
  strlcpy(cfg.mqttUser, doc["mqttUser"] | cfg.mqttUser, sizeof(cfg.mqttUser));
  strlcpy(cfg.mqttPass, doc["mqttPass"] | cfg.mqttPass, sizeof(cfg.mqttPass));
  cfg.pidKp = doc["pidKp"] | cfg.pidKp;
  cfg.pidKi = doc["pidKi"] | cfg.pidKi;
  cfg.pidKd = doc["pidKd"] | cfg.pidKd;
  cfg.prevPidKp = doc["prevPidKp"] | cfg.prevPidKp;
  cfg.prevPidKi = doc["prevPidKi"] | cfg.prevPidKi;
  cfg.prevPidKd = doc["prevPidKd"] | cfg.prevPidKd;
  cfg.tuneQualityScore = doc["tuneQualityScore"] | cfg.tuneQualityScore;

  cfg.profileCount = 0;
  cfg.activeProfileIndex = 0;
  return true;
}

void StorageManager::save(const PersistentConfig& cfg) {
  JsonDocument doc;
  doc["localSetpointC"] = cfg.localSetpointC;
  doc["stageStartBandC"] = cfg.stageStartBandC;
  doc["manualStageMinutes"] = cfg.manualStageMinutes;
  doc["overTempC"] = cfg.overTempC;
  doc["controlLock"] = (uint8_t)cfg.controlLock;
  doc["mqttHost"] = cfg.mqttHost;
  doc["mqttPort"] = cfg.mqttPort;
  doc["mqttUser"] = cfg.mqttUser;
  doc["mqttPass"] = cfg.mqttPass;
  doc["pidKp"] = cfg.pidKp;
  doc["pidKi"] = cfg.pidKi;
  doc["pidKd"] = cfg.pidKd;
  doc["prevPidKp"] = cfg.prevPidKp;
  doc["prevPidKi"] = cfg.prevPidKi;
  doc["prevPidKd"] = cfg.prevPidKd;
  doc["tuneQualityScore"] = cfg.tuneQualityScore;
  doc["activeProfileIndex"] = 0;


  String out;
  serializeJson(doc, out);
  _prefs.putString("cfg", out);
}
