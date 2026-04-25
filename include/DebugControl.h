#pragma once
#include <Arduino.h>
#include <stdarg.h>

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

static inline decltype(Serial)& debugStream() {
  return Serial;
}

static inline void debugBegin(unsigned long baud) {
  debugStream().begin(baud);
  const uint32_t started = millis();
  while (millis() - started < 1500) {
    if (debugStream()) break;
    delay(10);
  }
}

static inline void debugPrintf(const char* fmt, ...) {
  if (!gDebugEnabled) return;
  char buffer[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  debugStream().print(buffer);
}

#define DBG_LOGF(...) do { if (gDebugEnabled) debugPrintf(__VA_ARGS__); } while (0)
#define DBG_LOGLN(x) do { if (gDebugEnabled) debugStream().println(x); } while (0)
#define DBG_LOG(x) do { if (gDebugEnabled) debugStream().print(x); } while (0)
