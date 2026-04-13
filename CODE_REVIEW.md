# Full Code Review + Portability Extraction (2026-04-13)

## Scope reviewed
- Application entrypoint/orchestration in `src/main.cpp`.
- Reusable manager modules: `AlarmManager`, `AppState`, `DebugControl`, `MQTTManager`, `StageManager`, `StorageManager`, `WifiManagerWrapper`.
- Hardware/UI layers (`DisplayManager`, M5Dial boot/input usage).

## Findings

### 1) Reusable core code had an implicit M5Dial coupling through `Config.h`
Even though reusable managers did not directly include `M5Dial.h`, they depended on `Config.h`, which mixed:
- **portable runtime/business constants** (PID defaults, profile limits, MQTT timings)
- **M5Dial device-pin constants** (`PIN_ONEWIRE`, `PIN_HEATER`, `PIN_BUZZER`)

That made reuse in other device targets harder because importing manager code also imported device-specific assumptions.

### 2) M5Dial-specific logic is concentrated in UI/input/bootstrap paths
Dial-specific surface area remains in:
- `src/main.cpp` (`M5Dial.begin`, button/encoder/touch handling, speaker tones)
- `DisplayManager` (`M5Dial.Display`/`M5Dial.Touch` rendering & hit-testing)

This is expected and should remain in the M5Dial platform adapter layer.

### 3) Reusable managers are otherwise mostly portable
The listed reusable modules are largely hardware-agnostic after config separation.
Remaining platform affinity is ESP32/network stack level (WiFi/Preferences/PubSubClient), not M5Dial-specific.

## Implemented extraction (Option A: core + platform layer in-repo)

### New core library surface
- Added `include/core/CoreConfig.h` for portable defaults, limits, and protocol constants.

### New M5Dial platform surface
- Added `include/platform/m5dial/M5DialDeviceConfig.h` for M5Dial pin mapping.

### Compatibility bridge
- `include/Config.h` now acts as a composition shim that re-exports both namespaces for existing app code.

### Decoupled reusable modules from M5Dial config
These modules now depend on `CoreConfig` rather than the mixed `Config` namespace:
- `include/AppState.h`
- `src/MqttManager.cpp`
- `src/StageManager.cpp`
- `src/StorageManager.cpp`
- `src/WifiManagerWrapper.cpp`

## Result
- The reusable manager stack no longer consumes M5Dial pin config.
- M5Dial-specific constants now live in a dedicated platform header.
- Existing app behavior remains intact through `Config.h` compatibility exports.
- This structure is ready for a future `platform/m5atomswitch/*` adapter while reusing the same core managers.
