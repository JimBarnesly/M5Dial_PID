#include "CommandRouter.h"

#include <cstdlib>
#include <cstring>

namespace CommandSuffix {
constexpr char CommandTopic[] = "/cmd";
constexpr char Setpoint[] = "/cmd/setpoint";
constexpr char MqttHost[] = "/cmd/mqtt_host";
constexpr char OverTemp[] = "/cmd/over_temp";
constexpr char ControlLock[] = "/cmd/control_lock";
constexpr char MqttPort[] = "/cmd/mqtt_port";
constexpr char MqttTls[] = "/cmd/mqtt_tls";
constexpr char MqttTimeout[] = "/cmd/mqtt_timeout";
constexpr char MqttFallback[] = "/cmd/mqtt_fallback";
constexpr char WifiPortalTimeout[] = "/cmd/wifi_portal_timeout";
constexpr char ResetWifi[] = "/cmd/reset_wifi";
constexpr char Pid[] = "/cmd/pid";
constexpr char PidKp[] = "/cmd/pid_kp";
constexpr char PidKi[] = "/cmd/pid_ki";
constexpr char PidKd[] = "/cmd/pid_kd";
constexpr char GetConfig[] = "/cmd/get_config";
constexpr char GetEvents[] = "/cmd/get_events";
constexpr char ProfileSelect[] = "/cmd/profile_select";
constexpr char ProfileStart[] = "/cmd/profile_start";
constexpr char ProfileDelete[] = "/cmd/profile_delete";
constexpr char ProfileUpsert[] = "/cmd/profile_upsert";
constexpr char Minutes[] = "/cmd/minutes";
constexpr char Start[] = "/cmd/start";
constexpr char Pause[] = "/cmd/pause";
constexpr char Stop[] = "/cmd/stop";
constexpr char ResetAlarm[] = "/cmd/reset_alarm";
constexpr char AckAlarm[] = "/cmd/ack_alarm";
constexpr char StartAutotune[] = "/cmd/start_autotune";
constexpr char AcceptTune[] = "/cmd/accept_tune";
constexpr char RejectTune[] = "/cmd/reject_tune";
constexpr char TempCalibration[] = "/cmd/temp_calibration";
constexpr char CalibrationStatus[] = "/cmd/calibration_status";
}

namespace {
bool finitePositive(float v) {
  return isfinite(v) && v > 0.0f;
}

float clampSetpointToLimits(const PersistentConfig& cfg, float requested) {
  const float minLimit = min(cfg.minSetpointC, cfg.maxSetpointC);
  const float maxLimit = max(cfg.minSetpointC, cfg.maxSetpointC);
  return constrain(requested, minLimit, maxLimit);
}
}

void routeMqttCommand(const char* topic, const char* payload, CommandRouterServices& services) {
  const char* safeTopic = topic ? topic : "";
  const char* safePayload = payload ? payload : "";
  String t(safeTopic);
  String payloadText(safePayload);
  JsonDocument doc;
  DeserializationError parseErr = deserializeJson(doc, payloadText);
  if (parseErr) {
    String compact = payloadText;
    compact.trim();
    if (compact.startsWith("{") && compact.endsWith("}") && compact.indexOf(':') < 0) {
      compact = compact.substring(1, compact.length() - 1);
      compact.trim();
      if (compact.length() > 0) parseErr = DeserializationError::Ok, doc[compact] = true;
    } else if (compact.length() > 0 && compact.indexOf('{') < 0 && compact.indexOf(':') < 0) {
      parseErr = DeserializationError::Ok;
      doc[compact] = true;
    }
  }
  if (parseErr) return;

  if (t.endsWith(CommandSuffix::CommandTopic)) {
    String cmdKey;
    if (doc["command"].is<const char*>()) cmdKey = doc["command"].as<const char*>();

    if (cmdKey.length() == 0) {
      JsonObject obj = doc.as<JsonObject>();
      if (!obj.isNull() && obj.size() == 1) {
        cmdKey = obj.begin()->key().c_str();
      }
    }

    if (cmdKey.equalsIgnoreCase("run")) cmdKey = "start";
    if (cmdKey.equalsIgnoreCase("setpoint") && !doc["setpoint"].isNull()) doc["setpointC"] = doc["setpoint"];
    if (cmdKey.equalsIgnoreCase("mqtt_host") && !doc["mqtt_host"].isNull()) doc["host"] = doc["mqtt_host"];
    if (cmdKey.equalsIgnoreCase("over_temp") && !doc["over_temp"].isNull()) doc["overTempC"] = doc["over_temp"];
    if (cmdKey.equalsIgnoreCase("mqtt_port") && !doc["mqtt_port"].isNull()) doc["port"] = doc["mqtt_port"];
    if (cmdKey.equalsIgnoreCase("mqtt_tls") && !doc["mqtt_tls"].isNull()) doc["enabled"] = doc["mqtt_tls"];
    if (cmdKey.equalsIgnoreCase("mqtt_timeout") && !doc["mqtt_timeout"].isNull()) doc["seconds"] = doc["mqtt_timeout"];
    if (cmdKey.equalsIgnoreCase("wifi_portal_timeout") && !doc["wifi_portal_timeout"].isNull()) doc["seconds"] = doc["wifi_portal_timeout"];
    if (cmdKey.equalsIgnoreCase("mqtt_fallback") && !doc["mqtt_fallback"].isNull()) doc["mode"] = doc["mqtt_fallback"];

    if (cmdKey.length() > 0) {
      cmdKey.trim();
      cmdKey.toLowerCase();
      if (!cmdKey.startsWith("/")) cmdKey = "/" + cmdKey;
      if (!cmdKey.startsWith("/cmd/")) cmdKey = "/cmd" + cmdKey;
      t = services.mqtt.topicBase() + cmdKey;
    }
  }

  const char* command = "unknown";
  bool accepted = false;
  bool applied = true;
  const char* reason = "ok";
  const char* cmdId = doc["cmdId"] | "";
  const bool controlLockedLocalOnly = services.cfg.controlLock == ControlLock::LocalOnly;
  const bool localAuthorityOverrideActive =
      services.rt.operatingMode == OperatingMode::Integrated && services.cfg.localAuthorityOverride;

  auto parsePayloadFloat = [&](float fallback) -> float {
    char* endPtr = nullptr;
    const float parsed = strtof(safePayload, &endPtr);
    if (endPtr != safePayload && endPtr && *endPtr == '\0' && isfinite(parsed)) return parsed;
    return fallback;
  };

  auto parsePayloadInt = [&](int fallback) -> int {
    char* endPtr = nullptr;
    const long parsed = strtol(safePayload, &endPtr, 10);
    if (endPtr != safePayload && endPtr && *endPtr == '\0') return static_cast<int>(parsed);
    return fallback;
  };

  auto finishAck = [&]() {
    services.mqtt.publishCommandAck(cmdId,
                                    command,
                                    accepted,
                                    applied,
                                    reason,
                                    services.rt,
                                    services.stages.getRemainingSeconds());
  };

  if (t.endsWith(CommandSuffix::Setpoint)) {
    command = "setpoint";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else if (services.rt.uiMode == UiMode::SetpointAdjust || services.rt.runState == RunState::Idle ||
               services.rt.runState == RunState::Complete) {
      const float requested = doc["setpointC"] | parsePayloadFloat(NAN);
      if (!isfinite(requested) || requested < -20.0f || requested > 140.0f) {
        applied = false;
        reason = "invalid_range_setpoint";
      } else if (localAuthorityOverrideActive) {
        applied = false;
        reason = "local_authority_override";
      } else {
        services.rt.controlMode = ControlMode::Remote;
        services.cfg.localSetpointC = clampSetpointToLimits(services.cfg, requested);
        services.rt.currentSetpointC = services.cfg.localSetpointC;
        services.storage.save(services.cfg);
        services.display.requestImmediateUi();
      }
    } else {
      applied = false;
      reason = "wrong_run_state";
    }
  } else if (t.endsWith(CommandSuffix::MqttHost)) {
    command = "mqtt_host";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else if (!doc["host"].is<const char*>()) {
      applied = false;
      reason = "invalid_mqtt_host";
    } else {
      String requested = doc["host"].as<const char*>();
      requested.trim();
      if (requested.length() == 0 || requested.length() >= static_cast<int>(sizeof(services.cfg.mqttHost))) {
        applied = false;
        reason = "invalid_mqtt_host";
      } else {
        strlcpy(services.cfg.mqttHost, requested.c_str(), sizeof(services.cfg.mqttHost));
        services.storage.save(services.cfg);
        if (services.rt.mqttConnected) services.mqtt.publishConfig(services.cfg, services.rt);
        services.display.invalidateAll();
      }
    }
  } else if (t.endsWith(CommandSuffix::OverTemp)) {
    command = "over_temp";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      const float v = doc["overTempC"] | parsePayloadFloat(NAN);
      if (isfinite(v) && v >= 20.0f && v <= 140.0f) {
        services.cfg.overTempC = v;
        services.storage.save(services.cfg);
        if (services.rt.mqttConnected) services.mqtt.publishConfig(services.cfg, services.rt);
        services.display.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_range_over_temp";
      }
    }
  } else if (t.endsWith(CommandSuffix::ControlLock)) {
    command = "control_lock";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      int lockRaw = doc["controlLock"] | parsePayloadInt(-1);
      if (lockRaw >= 0 && lockRaw <= 2) {
        services.cfg.controlLock = static_cast<ControlLock>(lockRaw);
        if (services.cfg.controlLock == ControlLock::RemoteOnly) services.rt.controlMode = ControlMode::Remote;
        else if (services.rt.controlMode == ControlMode::Remote) services.rt.controlMode = ControlMode::Local;
        services.storage.save(services.cfg);
        if (services.rt.mqttConnected) services.mqtt.publishConfig(services.cfg, services.rt);
        services.display.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_control_lock";
      }
    }
  } else if (t.endsWith(CommandSuffix::MqttPort)) {
    command = "mqtt_port";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      int port = doc["port"] | parsePayloadInt(-1);
      if (port > 0 && port <= 65535) {
        services.cfg.mqttPort = static_cast<uint16_t>(port);
        services.storage.save(services.cfg);
        if (services.rt.mqttConnected) services.mqtt.publishConfig(services.cfg, services.rt);
        services.display.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_mqtt_port";
      }
    }
  } else if (t.endsWith(CommandSuffix::MqttTls)) {
    command = "mqtt_tls";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      const int tls = doc["enabled"] | parsePayloadInt(-1);
      if (tls == 0 || tls == 1) {
        services.cfg.mqttUseTls = (tls == 1);
        services.storage.save(services.cfg);
        if (services.rt.mqttConnected) services.mqtt.publishConfig(services.cfg, services.rt);
        services.display.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_mqtt_tls";
      }
    }
  } else if (t.endsWith(CommandSuffix::MqttTimeout)) {
    command = "mqtt_timeout";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      const int timeout = doc["seconds"] | parsePayloadInt(-1);
      if (timeout >= 0 && timeout <= 3600) {
        services.cfg.mqttCommsTimeoutSec = static_cast<uint16_t>(timeout);
        services.storage.save(services.cfg);
        if (services.rt.mqttConnected) services.mqtt.publishConfig(services.cfg, services.rt);
        services.display.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_mqtt_timeout";
      }
    }
  } else if (t.endsWith(CommandSuffix::MqttFallback)) {
    command = "mqtt_fallback";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      int mode = doc["mode"] | parsePayloadInt(-1);
      if (mode >= static_cast<int>(MqttFallbackMode::HoldSetpoint) &&
          mode <= static_cast<int>(MqttFallbackMode::StopHeater)) {
        services.cfg.mqttFallbackMode = static_cast<MqttFallbackMode>(mode);
        services.storage.save(services.cfg);
        if (services.rt.mqttConnected) services.mqtt.publishConfig(services.cfg, services.rt);
        services.display.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_mqtt_fallback";
      }
    }
  } else if (t.endsWith(CommandSuffix::WifiPortalTimeout)) {
    command = "wifi_portal_timeout";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      const int timeout = doc["seconds"] | parsePayloadInt(-1);
      if (timeout >= 30 && timeout <= 1800) {
        services.cfg.wifiPortalTimeoutSec = static_cast<uint16_t>(timeout);
        services.storage.save(services.cfg);
        if (services.rt.mqttConnected) services.mqtt.publishConfig(services.cfg, services.rt);
        services.display.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_wifi_timeout";
      }
    }
  } else if (t.endsWith(CommandSuffix::ResetWifi)) {
    command = "reset_wifi";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      services.wifi.resetSettings();
      services.logRuntimeEvent("WiFi settings reset (remote)");
    }
  } else if (t.endsWith(CommandSuffix::Pid)) {
    command = "pid";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      const float kp = doc["kp"] | services.cfg.pidKp;
      const float ki = doc["ki"] | services.cfg.pidKi;
      const float kd = doc["kd"] | services.cfg.pidKd;
      if (services.candidateWithinGuardrails(kp, ki, kd)) {
        services.cfg.pidKp = kp;
        services.cfg.pidKi = ki;
        services.cfg.pidKd = kd;
        services.applyTunings(services.cfg.pidKp, services.cfg.pidKi, services.cfg.pidKd);
        services.storage.save(services.cfg);
        if (services.rt.mqttConnected) services.mqtt.publishConfig(services.cfg, services.rt);
        services.display.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_pid";
      }
    }
  } else if (t.endsWith(CommandSuffix::PidKp)) {
    command = "pid_kp";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      const float kp = doc["kp"] | parsePayloadFloat(NAN);
      if (finitePositive(kp) && kp <= 60.0f) {
        services.cfg.pidKp = kp;
        services.applyTunings(services.cfg.pidKp, services.cfg.pidKi, services.cfg.pidKd);
        services.storage.save(services.cfg);
        if (services.rt.mqttConnected) services.mqtt.publishConfig(services.cfg, services.rt);
        services.display.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_pid_kp";
      }
    }
  } else if (t.endsWith(CommandSuffix::PidKi)) {
    command = "pid_ki";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      const float ki = doc["ki"] | parsePayloadFloat(NAN);
      if (finitePositive(ki) && ki <= 2.0f) {
        services.cfg.pidKi = ki;
        services.applyTunings(services.cfg.pidKp, services.cfg.pidKi, services.cfg.pidKd);
        services.storage.save(services.cfg);
        if (services.rt.mqttConnected) services.mqtt.publishConfig(services.cfg, services.rt);
        services.display.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_pid_ki";
      }
    }
  } else if (t.endsWith(CommandSuffix::PidKd)) {
    command = "pid_kd";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
    } else {
      const float kd = doc["kd"] | parsePayloadFloat(NAN);
      if (finitePositive(kd) && kd <= 80.0f) {
        services.cfg.pidKd = kd;
        services.applyTunings(services.cfg.pidKp, services.cfg.pidKi, services.cfg.pidKd);
        services.storage.save(services.cfg);
        if (services.rt.mqttConnected) services.mqtt.publishConfig(services.cfg, services.rt);
        services.display.invalidateAll();
      } else {
        applied = false;
        reason = "invalid_pid_kd";
      }
    }
  } else if (t.endsWith(CommandSuffix::GetConfig)) {
    command = "get_config";
    accepted = true;
    if (services.rt.mqttConnected) services.mqtt.publishConfig(services.cfg, services.rt);
  } else if (t.endsWith(CommandSuffix::GetEvents)) {
    command = "get_events";
    accepted = true;
    if (services.rt.mqttConnected) services.mqtt.publishEventLog(services.rt);
  } else if (t.endsWith(CommandSuffix::ProfileSelect)) {
    command = "profile_select";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    if (localAuthorityOverrideActive) {
      applied = false;
      reason = "local_authority_override";
      finishAck();
      return;
    }
    const int index = doc["index"] | -1;
    if (index < 0 || index >= services.cfg.profileCount) {
      applied = false;
      reason = "invalid_profile_index";
      finishAck();
      return;
    }
    services.cfg.activeProfileIndex = static_cast<uint8_t>(index);
    services.storage.save(services.cfg);
    services.logRuntimeEvent("Profile selected");
    services.display.invalidateAll();
  } else if (t.endsWith(CommandSuffix::ProfileStart)) {
    command = "profile_start";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    if (localAuthorityOverrideActive) {
      applied = false;
      reason = "local_authority_override";
      finishAck();
      return;
    }
    const int index = doc["index"] | static_cast<int>(services.cfg.activeProfileIndex);
    if (index < 0 || index >= services.cfg.profileCount) {
      applied = false;
      reason = "invalid_profile_index";
      finishAck();
      return;
    }
    services.cfg.activeProfileIndex = static_cast<uint8_t>(index);
    services.stages.startProfile(services.cfg.activeProfileIndex);
    services.completionHandled = false;
    services.logRuntimeEvent("Profile run started");
    services.storage.save(services.cfg);
    services.display.invalidateAll();
  } else if (t.endsWith(CommandSuffix::ProfileDelete)) {
    command = "profile_delete";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    const int index = doc["index"] | -1;
    if (index < 0 || index >= services.cfg.profileCount) {
      applied = false;
      reason = "invalid_profile_index";
      finishAck();
      return;
    }
    for (int i = index; i < services.cfg.profileCount - 1; ++i) {
      services.cfg.profiles[i] = services.cfg.profiles[i + 1];
    }
    if (services.cfg.profileCount > 0) --services.cfg.profileCount;
    if (services.cfg.profileCount == 0) services.cfg.activeProfileIndex = 0;
    else if (services.cfg.activeProfileIndex >= services.cfg.profileCount) services.cfg.activeProfileIndex = services.cfg.profileCount - 1;
    services.storage.save(services.cfg);
    services.logRuntimeEvent("Profile deleted");
    services.display.invalidateAll();
  } else if (t.endsWith(CommandSuffix::ProfileUpsert)) {
    command = "profile_upsert";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    uint8_t outIndex = 0;
    if (!services.upsertProfileFromJson(doc, &outIndex)) {
      applied = false;
      reason = "invalid_profile_payload";
      finishAck();
      return;
    }
    services.storage.save(services.cfg);
    if (services.rt.mqttConnected) services.mqtt.publishConfig(services.cfg, services.rt);
    services.logRuntimeEvent("Profile upserted");
    services.display.invalidateAll();
  } else if (t.endsWith(CommandSuffix::Minutes)) {
    command = "minutes";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    if (localAuthorityOverrideActive) {
      applied = false;
      reason = "local_authority_override";
      finishAck();
      return;
    }
    const int32_t mins = doc["minutes"] | parsePayloadInt(-1);
    if (mins < 0 || mins > 480) {
      applied = false;
      reason = "invalid_range_minutes";
      finishAck();
      return;
    }
    services.rt.desiredMinutes = static_cast<uint32_t>(mins);
    services.cfg.manualStageMinutes = static_cast<uint32_t>(mins);
    services.rt.activeStageMinutes = services.cfg.manualStageMinutes;
    services.storage.save(services.cfg);
    services.display.invalidateAll();
  } else if (t.endsWith(CommandSuffix::Start)) {
    command = "start";
    strlcpy(services.rt.desiredRunAction, "start", sizeof(services.rt.desiredRunAction));
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    if (localAuthorityOverrideActive) {
      applied = false;
      reason = "local_authority_override";
      finishAck();
      return;
    }
    if (services.rt.runState == RunState::Running || services.rt.runState == RunState::AutoTune || services.rt.runState == RunState::Fault) {
      applied = false;
      reason = "wrong_run_state";
      finishAck();
      return;
    }
    services.stages.start();
    services.completionHandled = false;
    services.logRuntimeEvent("Run started");
    services.display.invalidateAll();
  } else if (t.endsWith(CommandSuffix::Pause)) {
    command = "pause";
    strlcpy(services.rt.desiredRunAction, "pause", sizeof(services.rt.desiredRunAction));
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    if (localAuthorityOverrideActive) {
      applied = false;
      reason = "local_authority_override";
      finishAck();
      return;
    }
    if (services.rt.runState != RunState::Running) {
      applied = false;
      reason = "wrong_run_state";
      finishAck();
      return;
    }
    services.stages.pause();
    services.logRuntimeEvent("Run paused");
    services.display.invalidateAll();
  } else if (t.endsWith(CommandSuffix::Stop)) {
    command = "stop";
    strlcpy(services.rt.desiredRunAction, "stop", sizeof(services.rt.desiredRunAction));
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    if (localAuthorityOverrideActive) {
      applied = false;
      reason = "local_authority_override";
      finishAck();
      return;
    }
    if (services.rt.runState == RunState::AutoTune) {
      applied = false;
      reason = "wrong_run_state";
      finishAck();
      return;
    }
    services.stages.stop();
    services.completionHandled = false;
    services.logRuntimeEvent("Run stopped");
    services.display.invalidateAll();
  } else if (t.endsWith(CommandSuffix::ResetAlarm)) {
    command = "reset_alarm";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    services.alarm.clearAlarm(AlarmControlSource::RemoteMqtt);
    services.syncAlarmFromManager();
    services.logRuntimeEvent("Alarm reset (remote)");
    services.display.invalidateAll();
  } else if (t.endsWith(CommandSuffix::AckAlarm)) {
    command = "ack_alarm";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    if (!services.alarm.acknowledge(AlarmControlSource::RemoteMqtt)) {
      applied = false;
      reason = "ack_not_allowed";
      finishAck();
      return;
    }
    services.syncAlarmFromManager();
    services.logRuntimeEvent("Alarm acknowledged (remote)");
    services.display.invalidateAll();
  } else if (t.endsWith(CommandSuffix::StartAutotune)) {
    command = "start_autotune";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    if (services.rt.operatingMode == OperatingMode::Integrated && !services.cfg.localAuthorityOverride) {
      applied = false;
      reason = "requires_local_authority_override";
      finishAck();
      return;
    }
    if (!services.rt.sensorHealthy || isnan(services.rt.currentTempC) ||
        services.rt.runState == RunState::Running || services.rt.runState == RunState::Paused) {
      applied = false;
      reason = "wrong_run_state";
      finishAck();
      return;
    }
    if (!services.startAutoTune()) {
      applied = false;
      reason = "autotune_start_failed";
      finishAck();
      return;
    }
    services.display.invalidateAll();
  } else if (t.endsWith(CommandSuffix::AcceptTune)) {
    command = "accept_tune";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    if (services.rt.autoTunePhase != AutoTunePhase::PendingAccept) {
      applied = false;
      reason = "wrong_run_state";
      finishAck();
      return;
    }
    services.rt.previousKp = services.cfg.pidKp;
    services.rt.previousKi = services.cfg.pidKi;
    services.rt.previousKd = services.cfg.pidKd;
    services.persistActivePidAndQuality();
    services.rt.autoTunePhase = AutoTunePhase::Complete;
    services.logRuntimeEvent("Autotune accepted");
    if (services.rt.mqttConnected) services.mqtt.publishConfig(services.cfg, services.rt);
    services.display.invalidateAll();
  } else if (t.endsWith(CommandSuffix::RejectTune)) {
    command = "reject_tune";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    if (services.rt.autoTunePhase != AutoTunePhase::PendingAccept) {
      applied = false;
      reason = "wrong_run_state";
      finishAck();
      return;
    }
    services.applyTunings(services.cfg.pidKp, services.cfg.pidKi, services.cfg.pidKd);
    services.rt.autoTunePhase = AutoTunePhase::Failed;
    services.rt.autoTuneQualityScore = 0.0f;
    services.logRuntimeEvent("Autotune rejected");
    services.display.invalidateAll();
  } else if (t.endsWith(CommandSuffix::TempCalibration)) {
    command = "temp_calibration";
    accepted = true;
    if (controlLockedLocalOnly) {
      applied = false;
      reason = "control_lock_local_only";
      finishAck();
      return;
    }
    const float requestedOffset = doc["tempOffsetC"] | parsePayloadFloat(services.cfg.tempOffsetC);
    const bool hasSmoothing = !doc["tempSmoothingAlpha"].isNull();
    const bool hasOffset = !doc["tempOffsetC"].isNull();

    services.cfg.tempOffsetC = constrain(hasOffset ? requestedOffset : services.cfg.tempOffsetC, -10.0f, 10.0f);
    if (hasSmoothing) {
      services.cfg.tempSmoothingAlpha = constrain(static_cast<float>(doc["tempSmoothingAlpha"]), 0.0f, 1.0f);
    }

    services.tempSensor.setCalibrationOffset(services.cfg.tempOffsetC);
    services.tempSensor.setSmoothingFactor(services.cfg.tempSmoothingAlpha);
    services.storage.save(services.cfg);
    if (services.rt.mqttConnected) services.mqtt.publishCalibrationStatus(services.cfg, services.rt);
  } else if (t.endsWith(CommandSuffix::CalibrationStatus)) {
    command = "calibration_status";
    accepted = true;
    if (services.rt.mqttConnected) services.mqtt.publishCalibrationStatus(services.cfg, services.rt);
  }

  if (accepted) {
    services.rt.lastAcceptedRemoteCommandAtMs = millis();
  }

  finishAck();
}
