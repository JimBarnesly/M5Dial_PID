#pragma once
#include <Arduino.h>

extern bool gDebugEnabled;
extern bool gDebugDisableWifi;
extern bool gDebugDisableMqtt;
extern bool gDebugVerboseInput;

#define DBG_LOGF(...) do { if (gDebugEnabled) Serial.printf(__VA_ARGS__); } while (0)
#define DBG_LOGLN(x) do { if (gDebugEnabled) Serial.println(x); } while (0)
#define DBG_LOG(x) do { if (gDebugEnabled) Serial.print(x); } while (0)
