#pragma once

#include <Arduino.h>

namespace CoreConfig {
constexpr bool HEATER_ACTIVE_HIGH = true;
constexpr float DEFAULT_SETPOINT_C = 65.0f;
constexpr float DEFAULT_STAGE_START_BAND_C = 2.0f;
constexpr uint32_t DEFAULT_STAGE_MINUTES = 60;
constexpr float STAGE_AT_TEMP_BAND_C = 0.3f;
constexpr float DEFAULT_OVER_TEMP_C = 99.0f;
constexpr float SENSOR_FAULT_LOW_C = -10.0f;
constexpr float SENSOR_FAULT_HIGH_C = 150.0f;
constexpr float DEFAULT_TEMP_SMOOTHING_ALPHA = 0.25f;
constexpr float DEFAULT_TEMP_MAX_RATE_C_PER_SEC = 3.0f;
constexpr uint32_t PID_WINDOW_MS = 2000;
constexpr float PID_KP = 18.0f;
constexpr float PID_KI = 0.08f;
constexpr float PID_KD = 20.0f;
constexpr uint32_t TEMP_SAMPLE_MS = 1000;
constexpr uint32_t MQTT_RECONNECT_MS = 5000;
constexpr uint32_t DEFAULT_MQTT_COMMS_TIMEOUT_SEC = 30;
constexpr uint16_t DEFAULT_WIFI_PORTAL_TIMEOUT_SEC = 180;
constexpr uint32_t STATUS_PUBLISH_MS = 2000;
constexpr uint32_t ALARM_BEEP_MS = 700;
constexpr uint32_t BUZZER_TOGGLE_MS = 50;
constexpr uint16_t BUZZER_ALARM_FREQ_HZ = 2200;
constexpr uint16_t BUZZER_COMPLETION_FREQ_HZ = 2600;
constexpr uint32_t BUZZER_COMPLETION_BEEP_MS = 120;
constexpr uint32_t BUZZER_COMPLETION_GAP_MS = 90;
constexpr uint8_t BUZZER_COMPLETION_BEEP_COUNT = 3;
constexpr uint32_t TEMP_RISE_EVAL_MS = 120000;
constexpr float MIN_EXPECTED_RISE_C = 0.2f;
constexpr uint32_t UI_SERVICE_MS = 33;
constexpr uint32_t UI_TEMP_TEXT_MS = 1000;
constexpr uint32_t UI_STATUS_TEXT_MS = 250;
constexpr uint32_t UI_RING_MS = 1000;
constexpr uint32_t UI_FORCE_REFRESH_MS = 30000;
constexpr uint8_t MAX_STAGES = 8;
constexpr uint8_t MAX_PROFILES = 6;
constexpr uint8_t EVENT_LOG_CAPACITY = 24;
constexpr char WIFI_AP_NAME_PREFIX[] = "EnvCtrl-";
constexpr char WIFI_AP_PASS_PREFIX[] = "BC";
constexpr char FIRMWARE_VERSION[] = "v0.9.0-dev";
constexpr char MQTT_CLIENT_ID[] = "env_controller";
constexpr char MQTT_DEVICE_HOSTNAME[] = "HLT_PID";

inline String mqttTopicBase() {
  return String("/") + MQTT_DEVICE_HOSTNAME;
}
constexpr uint16_t MQTT_PORT_PLAIN = 1883;
constexpr uint16_t MQTT_PORT_TLS = 8883;
constexpr uint32_t UI_TEMP_MS = 1000;
}  // namespace CoreConfig
