# Ecosystem Architecture Audit (Commercial Modular Control Direction)

## 1. Executive summary

The repository is currently a **single-device firmware** that already contains good building blocks (local PID, stage/profile execution, persistent config, MQTT command/ack/status loop), but it is still shaped as a product prototype rather than a scalable ecosystem node.

The biggest architectural risks for the target operating model are:

1. **Network coupling to one forced SSID/password path (`project6`)** and forced reconnect strategy in Wi-Fi wrapper logic.
2. **Global/static MQTT topic base derived from a fixed device hostname (`/HLT_PID`)**, which blocks multi-system and multi-device-class scaling.
3. **Persistent MQTT host migration that currently rewrites to `10.42.0.1` on load**, reducing portability and making behavior surprising after firmware updates.
4. **Monolithic `main.cpp` ownership of orchestration, command dispatch, mode policy, and safety policy**, which will impede adding hub role(s) and other field device classes.
5. **No explicit system identity/hub identity model** in config or topics, making deliberate pairing/commissioning and anti-cross-pairing hard.

A pragmatic path is to preserve the existing local-control loop, then progressively introduce:
- a dedicated **identity + provisioning domain model**,
- a **network policy layer** (standalone vs hub-managed),
- a **topic namespace model** with `systemId` and `deviceId`,
- and a **split between local control engine and transport adapters**.

## 2. Current architecture map

### Main runtime topology
- `src/main.cpp` is the runtime composition root and contains:
  - global singleton instances for all subsystems,
  - startup initialization order,
  - input handling, safety, PID loop scheduling,
  - remote command parsing + routing,
  - periodic publish/save loops.

### Modules and responsibilities
- **State & config model**
  - `include/AppState.h` defines both `PersistentConfig` and `RuntimeState` (control lock/mode, MQTT fields, PID params, profiles, alarms, event log).
- **Config constants**
  - `include/core/CoreConfig.h` defines defaults, limits, timers, topic-base helper, and protocol constants.
- **Persistence**
  - `src/StorageManager.cpp` stores `PersistentConfig` JSON in Preferences namespace `env-ctrl` key `cfg`.
  - Includes conditional persistence of secrets based on flash encryption availability.
- **Networking / provisioning**
  - `src/WifiManagerWrapper.cpp` handles WiFiManager portal + saved credential persistence (`wifi-cred`) and forced network attempts.
- **MQTT transport**
  - `src/MqttManager.cpp` manages reconnect, subscriptions, status/ack/config/event publishing, TLS transport selection.
- **Control logic**
  - `src/PidController.cpp` computes bounded PID output.
  - `src/StageManager.cpp` controls manual and profile stage progression.
  - `src/TempSensor.cpp`, `src/HeaterOutput.cpp`, `src/AlarmManager.cpp` implement sensing/output/alarm signaling logic.
- **UI and hardware adapters**
  - `src/DisplayManager.cpp` and `platform/m5dial/*` bind behavior to M5Dial hardware.

### Startup and reconnect flow (as implemented)
1. Load persisted config.
2. Initialize sensor/PID/output/alarm/stage managers.
3. Start Wi-Fi wrapper (`gWifi.begin(...)`) unless debug-disabled.
4. Start MQTT manager and callback.
5. Enter loop: process input, service Wi-Fi, service MQTT, evaluate timeouts/safety/control, publish status, persist config.

## 3. High-risk issues

1. **Hardcoded Wi-Fi topology in field device firmware (design flaw).**
   - Forced SSID/password (`project6` / `sIlver@99`) are attempted first and during reconnect.
   - This makes the firmware tightly coupled to one deployment topology and can prevent clean operation in standalone or site-portable modes.
   - Impact: limits commercial portability, increases support burden, blocks deliberate commissioning model.

2. **MQTT topic namespace cannot scale across systems/devices (architecture flaw).**
   - Topic base is derived from a fixed constant hostname (`/HLT_PID`) rather than dynamic system/device identity.
   - Multiple installations or mixed device classes will collide semantically and operationally.
   - Impact: cross-system crosstalk risk and no clean path to modular ecosystem roles.

3. **Broker host migration behavior overwrites previous host to `10.42.0.1` (bug / data-loss behavior).**
   - During load, stored `mqttHost` is intentionally replaced with default and then saved if “migrated”.
   - This can silently alter deployed behavior and hides operator intent.
   - Impact: unpredictable reconnect target after reboot/update; harmful in non-hub test and service workflows.

4. **Security and pairing identity are missing (future-scale risk).**
   - No explicit machine-safe `systemId` + `hubId` + `deviceId` binding state in config model.
   - No anti-cross-pairing token model or lifecycle state (uncommissioned/commissioned/revoked).
   - Impact: NFC bootstrap direction cannot be layered cleanly without schema rework.

5. **Large command and orchestration monolith in `main.cpp` (technical debt with product risk).**
   - Command parsing, policy, persistence side effects, and runtime controls are mixed in a single function path.
   - Impact: difficult to test, difficult to add device roles, and high regression probability as ecosystem grows.

## 4. Medium-risk issues

1. **`defaultMqttHost/defaultMqttPort` args are unused in WiFi wrapper.**
   - Signals old assumptions or incomplete refactor; creates confusion on ownership boundaries.

2. **Topic compatibility layer is ad hoc.**
   - Supports `/command` envelope and legacy `/cmd/+`, but with implicit remapping in handler.
   - Good for prototype compatibility, weak for long-term contract governance.

3. **Control mode semantics are present but not fully separated by policy layer.**
   - `ControlLock`, `ControlMode`, and MQTT timeout fallback exist, but are enforced across mixed UI and command handlers in one orchestration file.

4. **Persistent save cadence is broad.**
   - Full config save in periodic loop every status interval plus command/UI-driven saves; mitigated by JSON compare cache, but still broad and implicit.

5. **Device role abstraction is not explicit.**
   - Current code implies one role (PID temperature controller) with no common field-device protocol interface.

## 5. Networking/provisioning findings

- Wi-Fi behavior is currently a mixed model:
  - force-connect to one SSID first,
  - fallback to saved credentials,
  - then non-blocking portal AP.
- Reconnect logic retries forced credentials every 10s when disconnected.
- Captive portal AP credentials are device-unique for onboarding AP mode.
- For hub-owned AP future, current code is partly aligned on default broker IP (`10.42.0.1`) but undermined by forced SSID coupling.
- Server addresses are host strings (`char mqttHost[64]`), which is compatible with stable IP or DNS, but current logic currently behaves as effectively fixed-IP with forced migration.
- NFC bootstrap readiness today is low because there is no commissioning payload schema or identity-binding lifecycle in the persistent model.

## 6. MQTT and identity findings

- Current subscriptions:
  - `<base>/command` and `<base>/cmd/+`.
- Current publications:
  - `<base>/state` with `_type` multiplexer (`status`, `shadow`, `cmd_ack`, etc.).
- Base topic currently from `CoreConfig::mqttTopicBase()` => `/HLT_PID`.
- MQTT client ID includes MAC suffix (good for uniqueness at broker client layer), but topic namespace identity is still static.
- For ecosystem expansion, topic envelope should evolve to include stable identifiers and versioned contracts.

## 7. Standalone vs hub-supervised control findings

- Local PID loop remains on device and is independent of MQTT command traffic (good and aligned).
- Remote supervision exists with mode/fallback behavior:
  - comms timeout alarm + fallback actions (`hold`, `pause`, `stop heater`).
- Loss-of-hub behavior is partially defined and mostly safe for heating output.
- Ownership boundary is conceptually correct (local loop on field device), but implementation boundary is blurred by orchestration coupling and lack of explicit policy modules.

## 8. Persistence/state-management findings

- Preferences namespaces:
  - `env-ctrl` for full JSON config,
  - `wifi-cred` for SSID/pass.
- Boot restore uses defaults + JSON overlay + profile parsing.
- Secret persistence is disabled when flash encryption is unavailable (good security posture, but can surprise operators if not surfaced clearly in UI/API).
- `mqttHost` forced migration behavior is the largest persistence correctness issue.
- Potential stale-behavior confusion source: mixed storage ownership (WiFiManager + custom Wi-Fi prefs + config JSON) plus forced reconnect path can make runtime behavior diverge from visible config assumptions.

## 9. Refactor roadmap

### Immediate (surgical)
1. Remove forced SSID/password fallback from production path; gate it behind explicit dev build flag.
2. Fix `StorageManager::load` so `mqttHost` is not unconditionally overwritten.
3. Introduce explicit `NetworkProvisioningMode` in persisted config (`StandaloneInfra`, `HubManagedAp`) with controlled transition rules.
4. Add structured diagnostics fields for current network policy source and last-commissioned identity.

### Short-term (incremental structure)
1. Extract command handling from `main.cpp` into `CommandRouter` + per-domain handlers (run control, config, profiles, tuning, diagnostics).
2. Introduce `IdentityManager` model (`systemId`, `systemName`, `hubId`, `deviceId`, `commissionedAt`).
3. Replace static topic base helper with runtime topic builder based on identity model.
4. Introduce configuration schema versioning and migration functions per version.

### Medium-term (architectural alignment)
1. Add commissioning state machine:
   - uncommissioned -> commissioned (bound to system/hub) -> reset/repair.
2. Add bootstrap payload ingestion interface (initially via MQTT/serial/test, later NFC card transport).
3. Add transport abstraction for hub-managed AP assumptions and optional upstream connectivity logic.
4. Separate field-device shared protocol package from device-role implementation package.

### Optional future
1. Mutual auth per device credentials issued during commissioning.
2. Signed commissioning payloads with expiry/nonces to avoid replay/cross-pairing.
3. Event-sourced state transitions for better fleet diagnostics.

## 10. Target architecture recommendation

### Recommended module boundaries
- `core/control/`:
  - PID, stage runner, safety state machine, failsafe policy.
- `core/domain/`:
  - config schema, identity schema, commissioning state.
- `core/protocol/`:
  - topic builder, command contracts, status contracts.
- `adapters/network/`:
  - Wi-Fi policy engine, MQTT transport.
- `adapters/platform/m5dial/`:
  - UI/input/hardware output only.
- `app/`:
  - composition root and scheduling.

### Ownership model
- **Config ownership:** schema + migrations in one place; adapters consume, not mutate ad hoc.
- **Networking ownership:** dedicated policy module decides how network credentials are sourced and retried.
- **MQTT ownership:** protocol module defines topics/payload contracts; transport module only sends/receives.
- **Control ownership:** local control engine authoritative for heater safety and loop timing; remote commands request intents.

### Provisioning flow toward NFC card bootstrap
1. Hub generates commissioning payload with `systemId`, `hubId`, AP credentials (or network token), broker endpoint, validity window, signature.
2. NFC card carries this payload.
3. Device ingests payload, validates signature/expiry, stores binding, switches provisioning mode to `HubManagedAp`.
4. Device publishes commissioning receipt on first MQTT connect.
5. Re-pair requires explicit local reset or authenticated remote reprovision command.

### Identity model (minimum fields)
- `systemId` (UUID, machine-safe, immutable once commissioned)
- `systemName` (user-facing mutable label)
- `hubId` (UUID)
- `deviceId` (factory unique)
- `deviceClass` (pid-temp, pump, valve, etc.)

### Topic pattern recommendation
- `systems/{systemId}/devices/{deviceId}/{channel}`
  - `.../cmd`, `.../state`, `.../events`
- Optional class-level fan-out:
  - `systems/{systemId}/classes/{deviceClass}/...`

## 11. Quick wins I can implement immediately

1. Remove or dev-flag the hardcoded forced SSID/password path.
2. Stop overwriting `mqttHost` during load migration.
3. Add explicit `systemId` + `systemName` to `PersistentConfig`.
4. Add dynamic topic-base builder using `systemId` and `deviceId` while keeping current topic path as compatibility alias.
5. Move `handleCommands` into a separate compilation unit with unit tests for command parsing and policy rules.
6. Add a startup diagnostic payload including effective network policy, commissioning state, and identity tuple.

## 12. Open questions / assumptions that need confirmation

1. Should standalone mode ever connect to arbitrary infrastructure Wi-Fi in production, or only temporary commissioning AP + hub AP?
2. Is `10.42.0.1` intended to be immutable for all hub deployments, or configurable per system while still deterministic?
3. Is one field device allowed to roam between hubs, or must binding be strict until factory-reset/repair workflow?
4. What security level is required at launch: plaintext MQTT allowed in any shipped mode, or TLS mandatory except lab builds?
5. Should local UI always retain emergency stop/alarm ack rights even in `RemoteOnly` mode?
6. For multi-hub same venue scenarios, do you prefer cryptographic commissioning tokens from day 1 or phased rollout?
