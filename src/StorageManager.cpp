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
  cfg.overTempC = 99.0f;
  strncpy(cfg.mqttHost, "192.168.1.10", sizeof(cfg.mqttHost)-1);
  cfg.mqttPort = 1883;
  cfg.profileCount = 1;
  cfg.activeProfileIndex = 0;

  BrewProfile& p = cfg.profiles[0];
  strncpy(p.name, "Default Mash", sizeof(p.name)-1);
  p.stageCount = 3;
  strncpy(p.stages[0].name, "Mash-In", sizeof(p.stages[0].name)-1);
  p.stages[0].targetC = 52.0f; p.stages[0].holdSeconds = 600;
  strncpy(p.stages[1].name, "Beta", sizeof(p.stages[1].name)-1);
  p.stages[1].targetC = 64.0f; p.stages[1].holdSeconds = 2400;
  strncpy(p.stages[2].name, "Mash-Out", sizeof(p.stages[2].name)-1);
  p.stages[2].targetC = 76.0f; p.stages[2].holdSeconds = 600;
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
  cfg.overTempC = doc["overTempC"] | cfg.overTempC;
  cfg.controlLock = static_cast<ControlLock>((uint8_t)(doc["controlLock"] | (uint8_t)cfg.controlLock));
  strlcpy(cfg.mqttHost, doc["mqttHost"] | cfg.mqttHost, sizeof(cfg.mqttHost));
  cfg.mqttPort = doc["mqttPort"] | cfg.mqttPort;
  strlcpy(cfg.mqttUser, doc["mqttUser"] | cfg.mqttUser, sizeof(cfg.mqttUser));
  strlcpy(cfg.mqttPass, doc["mqttPass"] | cfg.mqttPass, sizeof(cfg.mqttPass));

  JsonArray profiles = doc["profiles"].as<JsonArray>();
  if (!profiles.isNull()) {
    cfg.profileCount = 0;
    for (JsonObject p : profiles) {
      if (cfg.profileCount >= Config::MAX_PROFILES) break;
      BrewProfile& dst = cfg.profiles[cfg.profileCount++];
      memset(&dst, 0, sizeof(dst));
      strlcpy(dst.name, p["name"] | "Profile", sizeof(dst.name));
      JsonArray stages = p["stages"].as<JsonArray>();
      for (JsonObject s : stages) {
        if (dst.stageCount >= Config::MAX_STAGES) break;
        BrewStage& st = dst.stages[dst.stageCount++];
        strlcpy(st.name, s["name"] | "Stage", sizeof(st.name));
        st.targetC = s["targetC"] | 65.0f;
        st.holdSeconds = s["holdSeconds"] | 600;
      }
    }
  }
  cfg.activeProfileIndex = min<uint8_t>(doc["activeProfileIndex"] | 0, max<uint8_t>(cfg.profileCount,1)-1);
  return true;
}

void StorageManager::save(const PersistentConfig& cfg) {
  JsonDocument doc;
  doc["localSetpointC"] = cfg.localSetpointC;
  doc["stageStartBandC"] = cfg.stageStartBandC;
  doc["overTempC"] = cfg.overTempC;
  doc["controlLock"] = (uint8_t)cfg.controlLock;
  doc["mqttHost"] = cfg.mqttHost;
  doc["mqttPort"] = cfg.mqttPort;
  doc["mqttUser"] = cfg.mqttUser;
  doc["mqttPass"] = cfg.mqttPass;
  doc["activeProfileIndex"] = cfg.activeProfileIndex;

  JsonArray profiles = doc["profiles"].to<JsonArray>();
  for (uint8_t i = 0; i < cfg.profileCount; ++i) {
    JsonObject p = profiles.add<JsonObject>();
    p["name"] = cfg.profiles[i].name;
    JsonArray stages = p["stages"].to<JsonArray>();
    for (uint8_t j = 0; j < cfg.profiles[i].stageCount; ++j) {
      JsonObject s = stages.add<JsonObject>();
      s["name"] = cfg.profiles[i].stages[j].name;
      s["targetC"] = cfg.profiles[i].stages[j].targetC;
      s["holdSeconds"] = cfg.profiles[i].stages[j].holdSeconds;
    }
  }

  String out;
  serializeJson(doc, out);
  _prefs.putString("cfg", out);
}
