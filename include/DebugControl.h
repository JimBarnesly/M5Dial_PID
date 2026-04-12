#pragma once
#include <Arduino.h>

extern bool gDebugEnabled;
extern bool gDebugDisableWifi;
extern bool gDebugDisableMqtt;
extern bool gDebugVerboseInput;

static inline bool debugRuntimeNetworkTogglesEnabled() {
#ifdef DEBUG_ENABLE_RUNTIME_NET_TOGGLES
  return true;
#else
  return false;
#endif
}

static inline bool debugWifiDisabledEffective() {
#if defined(DEBUG_DISABLE_NETWORK) || defined(DEBUG_DISABLE_WIFI)
  return true;
#else
  return debugRuntimeNetworkTogglesEnabled() && gDebugDisableWifi;
#endif
}

static inline bool debugMqttDisabledEffective() {
#if defined(DEBUG_DISABLE_NETWORK) || defined(DEBUG_DISABLE_MQTT)
  return true;
#else
  return debugRuntimeNetworkTogglesEnabled() && gDebugDisableMqtt;
#endif
}

static inline const char* debugNetworkModeLabel() {
  if (debugWifiDisabledEffective() && debugMqttDisabledEffective()) return "network_disabled";
  if (debugWifiDisabledEffective()) return "wifi_disabled";
  if (debugMqttDisabledEffective()) return "mqtt_disabled";
  return "network_enabled";
}

#define DBG_LOGF(...) do { if (gDebugEnabled) Serial.printf(__VA_ARGS__); } while (0)
#define DBG_LOGLN(x) do { if (gDebugEnabled) Serial.println(x); } while (0)
#define DBG_LOG(x) do { if (gDebugEnabled) Serial.print(x); } while (0)
