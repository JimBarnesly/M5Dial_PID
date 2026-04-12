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
  cfg.profileCount = static_cast<uint8_t>(doc["profileCount"] | cfg.profileCount);
  cfg.activeProfileIndex = static_cast<uint8_t>(doc["activeProfileIndex"] | cfg.activeProfileIndex);

  JsonArray profiles = doc["profiles"].as<JsonArray>();
  if (!profiles.isNull()) {
    cfg.profileCount = 0;
    for (JsonObject p : profiles) {
      if (cfg.profileCount >= Config::MAX_PROFILES) break;
      BrewProfile& profile = cfg.profiles[cfg.profileCount];
      strlcpy(profile.name, p["name"] | "PROFILE", sizeof(profile.name));
      profile.stageCount = static_cast<uint8_t>(p["stageCount"] | 0);
      if (profile.stageCount > Config::MAX_STAGES) profile.stageCount = Config::MAX_STAGES;

      JsonArray stages = p["stages"].as<JsonArray>();
      uint8_t loadedStages = 0;
      if (!stages.isNull()) {
        for (JsonObject s : stages) {
          if (loadedStages >= profile.stageCount || loadedStages >= Config::MAX_STAGES) break;
          BrewStage& stage = profile.stages[loadedStages];
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
  doc["profileCount"] = cfg.profileCount;
  doc["activeProfileIndex"] = cfg.activeProfileIndex;

  JsonArray profiles = doc["profiles"].to<JsonArray>();
  const uint8_t profileCount = (cfg.profileCount > Config::MAX_PROFILES) ? Config::MAX_PROFILES : cfg.profileCount;
  for (uint8_t i = 0; i < profileCount; ++i) {
    const BrewProfile& profile = cfg.profiles[i];
    JsonObject p = profiles.add<JsonObject>();
    p["name"] = profile.name;
    const uint8_t stageCount = (profile.stageCount > Config::MAX_STAGES) ? Config::MAX_STAGES : profile.stageCount;
    p["stageCount"] = stageCount;
    JsonArray stages = p["stages"].to<JsonArray>();
    for (uint8_t j = 0; j < stageCount; ++j) {
      const BrewStage& stage = profile.stages[j];
      JsonObject s = stages.add<JsonObject>();
      s["name"] = stage.name;
      s["targetC"] = stage.targetC;
      s["holdSeconds"] = stage.holdSeconds;
    }
  }


  String out;
  serializeJson(doc, out);
  _prefs.putString("cfg", out);
}
