# M5Dial Environment Controller

Key changes in V3:
- Partial redraw UI instead of full-screen flashing.
- Setpoint edits redraw immediately.
- Temperature text redraw is throttled to 10 s.
- Ring redraw is once per second.
- Network/status text redraw is fast but localised.
- Touch handling removed for stability.
- Buzzer made non-blocking.
- Stage-complete notification/beep is one-shot.
- M5Dial library pulled directly from GitHub to avoid PlatformIO registry package issues.

Current UX behaviour:
- Center button toggles setpoint edit mode.
- Encoder changes setpoint instantly while in edit mode.
- Stage timer can be set to `0m` for indefinite temperature regulation (no auto-complete).
- Main background is static after boot.
- Current temp updates slowly by design.
- Time remaining and power update without full-screen flicker.

Settings updates:
- Settings icon opens editable runtime settings pages.
- Added on-device controls for MQTT comms timeout and fallback mode.
- Added configurable Wi-Fi portal timeout used during commissioning.
- Added remote profile lifecycle commands (`profile_upsert`, `profile_select`, `profile_start`, `profile_delete`).
- Added remote diagnostics event log publishing (`/event/log`, `/cmd/get_events`).
- Added autotune accept/reject command paths to commit or discard candidates.


V4 notes:
- display layer rewritten to follow the Stitch screen much more closely
- static chrome is drawn once, with only changed regions refreshed
- ring follows the outer thin countdown layout from the Stitch mock
- bottom icons are rendered with primitives instead of text glyphs for runtime stability

## MQTT integration

The firmware uses `CoreConfig::MQTT_TOPIC_BASE` as the topic prefix (default: `env/controller`). With the current defaults, the key MQTT topics are:

- **Commands in**: `env/controller/command`
- **Legacy commands in (still supported)**: `env/controller/cmd/+`
- **State/events out**: `env/controller/state`

### Subscribe / publish model

- Your integration should **publish commands** to `.../command` as JSON envelopes.
- The device subscribes to both `.../command` and `.../cmd/+` for backward compatibility.
- Your integration should **subscribe to** `.../state` to receive status, command acknowledgements, config snapshots, and event/log payloads.

### Command formats

You can send commands either way:

1. **Recommended envelope format** (publish to `.../command`)

```json
{"cmdId":"abc-123","command":"setpoint","setpoint":62.5}
```

2. **Legacy direct topic format** (publish to `.../cmd/setpoint`)

```json
{"cmdId":"abc-123","setpointC":62.5}
```

The command parser normalizes command keys from the envelope by:
- trimming and lowercasing,
- ensuring a leading `/`,
- ensuring `/cmd/` prefix before routing.

### Common commands

Supported command suffixes include:

- `/cmd/setpoint`
- `/cmd/start`, `/cmd/pause`, `/cmd/stop`
- `/cmd/minutes`
- `/cmd/over_temp`
- `/cmd/pid`, `/cmd/pid_kp`, `/cmd/pid_ki`, `/cmd/pid_kd`
- `/cmd/control_lock`
- `/cmd/mqtt_port`, `/cmd/mqtt_tls`, `/cmd/mqtt_timeout`, `/cmd/mqtt_fallback`
- `/cmd/wifi_portal_timeout`, `/cmd/reset_wifi`
- `/cmd/get_config`, `/cmd/get_events`
- `/cmd/profile_select`, `/cmd/profile_start`, `/cmd/profile_delete`, `/cmd/profile_upsert`
- `/cmd/start_autotune`, `/cmd/accept_tune`, `/cmd/reject_tune`
- `/cmd/temp_calibration`, `/cmd/calibration_status`

### Reading updates on `.../state`

Every outbound payload on `.../state` includes an `_type` field so you can route by message type:

- `_type: "status"` — runtime status snapshot (temperature, setpoint, run state, alarms, etc.)
- `_type: "shadow"` — desired vs reported state summary
- `_type: "cmd_ack"` — acknowledgement of a received command (`cmdId`, `accepted`, `applied`, `reason`)
- `_type: "config_effective"` — current effective device config
- `_type: "event_log"` — runtime event/diagnostic log data
- `_type: "calibration_status"` — sensor calibration state
- `_type: "profile_complete"` — emitted when a profile-complete publish is pending

### Minimal client checklist

1. Subscribe to `env/controller/state`.
2. Publish commands to `env/controller/command` with a unique `cmdId`.
3. Watch for `_type: "cmd_ack"` on `.../state` and match by `cmdId`.
4. Handle periodic `_type: "status"` updates and optional `_type` variants listed above.

## Secure commissioning and minimum safe defaults

This firmware generates onboarding AP credentials per-device and supports secure MQTT transport options.

### 1) First boot / Wi-Fi onboarding
- On first boot (or after clearing Wi-Fi credentials), the device starts a WiFiManager portal AP.
- The AP SSID is generated per-device from hardware identity.
- The AP password is derived from the device identity (MAC suffix) and printed in masked form on serial debug logs.
- In serial debug logs, the AP password is masked so full credentials are not exposed.

### 2) MQTT security modes
Set these `PersistentConfig` fields before deployment (via your existing config channel):
- `mqttUseTls=true` to enable TLS.
- `mqttPort`:
  - Use `8883` for TLS brokers.
  - If TLS is enabled and `mqttPort` is left at `1883`, firmware automatically uses `8883`.
- `mqttTlsAuthMode`:
  - `0`: TLS with insecure server auth (transport encryption only; not recommended outside test networks).
  - `1`: certificate fingerprint pinning using `mqttTlsFingerprint`.
  - `2`: CA certificate pinning using `mqttTlsCaCert` (PEM).

### 3) Secret handling / storage behavior
- Status publishing and debug output avoid printing plaintext secrets.
- MQTT command logs now print payload size only, not full payload content.
- Plaintext secret persistence is blocked unless encrypted flash storage is available:
  - `mqttUser`, `mqttPass`, `mqttTlsFingerprint`, and `mqttTlsCaCert` are saved only when flash encryption is enabled.
  - Without encrypted storage, those fields are kept in-memory for runtime use but not persisted.

### Minimum safe defaults (recommended)
- Record the device-specific onboarding credentials at install time and keep commissioning windows short in production.
- Use `mqttUseTls=true`, `mqttPort=8883`, and `mqttTlsAuthMode=2` (CA pinning) whenever possible.
- Avoid `mqttTlsAuthMode=0` except temporary lab testing.
