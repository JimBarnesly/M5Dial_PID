# Automation Controller MQTT Reference

This document is the current MQTT contract exposed by the firmware in this repository.

It is intended to be the working reference for a Node-RED automation controller.

## 1. Topic base

All device topics are rooted at:

```text
/systems/<system_id>/devices/pid/<device_id>
```

Examples:

```text
/systems/unbound/devices/pid/pid_01
/systems/greenhouse_a/devices/pid/hlt_01
```

Notes:

- `deviceClass` is currently fixed to `pid`.
- `system_id` comes from runtime integration state.
- `device_id` comes from persisted device config.

## 2. Main topics

Published by device:

- `<base>/status` retained
- `<base>/shadow` retained
- `<base>/config` retained
- `<base>/events` not retained
- `<base>/calibration` retained
- `<base>/lifecycle` retained
- `<base>/enrollment/request` not retained

Subscribed by device:

- `<base>/cmd`
- `<base>/cmd/+`
- `<base>/enrollment/response`
- `<base>/controller/heartbeat`

## 3. Command publish styles

The firmware accepts commands in either of these styles.

Aggregate command topic:

```json
Topic: <base>/cmd
Payload:
{
  "command": "start",
  "cmdId": "abc123"
}
```

Direct command topic:

```json
Topic: <base>/cmd/start
Payload:
{
  "cmdId": "abc123"
}
```

The aggregate `/cmd` topic is usually easiest for Node-RED.

## 4. Command acknowledgement

Every accepted command path publishes an acknowledgement to:

```text
<base>/config
```

with:

```json
{
  "_type": "cmd_ack",
  "cmdId": "abc123",
  "command": "start",
  "accepted": true,
  "applied": true,
  "reason": "ok",
  "reported": {
    "runState": "running",
    "setpointC": 65.0,
    "effectiveTimerSec": 3520
  }
}
```

Important:

- `accepted=false` is not currently used much; most failures come back as `accepted=true`, `applied=false`.
- Always check `applied` and `reason`.

## 5. Supported remote commands

### 5.1 Runtime control

`setpoint`

```json
Topic: <base>/cmd
{
  "command": "setpoint",
  "setpointC": 65.0,
  "cmdId": "setpoint_1"
}
```

Rules:

- Allowed only in `SetpointAdjust`, `Idle`, or `Complete`.
- Clamped to configured min/max setpoint limits.

`minutes`

```json
{
  "command": "minutes",
  "minutes": 60
}
```

Range:

- `0` to `480`

`start`

```json
{
  "command": "start"
}
```

`pause`

```json
{
  "command": "pause"
}
```

`stop`

```json
{
  "command": "stop"
}
```

Notes:

- Local stop is still allowed on-device.
- Commands are blocked if local-authority override is active in integrated mode.

### 5.2 Alarm control

`reset_alarm`

```json
{
  "command": "reset_alarm"
}
```

`ack_alarm`

```json
{
  "command": "ack_alarm"
}
```

### 5.3 MQTT / network config

`mqtt_host`

```json
{
  "command": "mqtt_host",
  "host": "10.42.0.1"
}
```

`mqtt_port`

```json
{
  "command": "mqtt_port",
  "port": 1883
}
```

`mqtt_tls`

```json
{
  "command": "mqtt_tls",
  "enabled": 0
}
```

`mqtt_timeout`

```json
{
  "command": "mqtt_timeout",
  "seconds": 30
}
```

`mqtt_fallback`

```json
{
  "command": "mqtt_fallback",
  "mode": 0
}
```

Fallback mode values:

- `0` = hold setpoint
- `1` = pause
- `2` = stop heater

`wifi_portal_timeout`

```json
{
  "command": "wifi_portal_timeout",
  "seconds": 180
}
```

Range:

- `30` to `1800`

`reset_wifi`

```json
{
  "command": "reset_wifi"
}
```

### 5.4 Control limits / safety

`over_temp`

```json
{
  "command": "over_temp",
  "overTempC": 99.0
}
```

Valid range:

- `20.0` to `140.0`

`control_lock`

```json
{
  "command": "control_lock",
  "controlLock": 2
}
```

Control lock values:

- `0` = local only
- `1` = remote only
- `2` = local or remote

### 5.5 PID tuning

`pid`

```json
{
  "command": "pid",
  "kp": 18.0,
  "ki": 0.08,
  "kd": 20.0
}
```

`pid_kp`

```json
{
  "command": "pid_kp",
  "kp": 18.0
}
```

Valid range:

- `> 0` and `<= 60.0`

`pid_ki`

```json
{
  "command": "pid_ki",
  "ki": 0.08
}
```

Valid range:

- `> 0` and `<= 2.0`

`pid_kd`

```json
{
  "command": "pid_kd",
  "kd": 20.0
}
```

Valid range:

- `> 0` and `<= 80.0`

### 5.6 Autotune

`start_autotune`

```json
{
  "command": "start_autotune"
}
```

Important:

- In integrated mode this currently requires local-authority override.
- It is rejected if the device is already running, paused, unhealthy, or otherwise not in a safe start state.

`accept_tune`

```json
{
  "command": "accept_tune"
}
```

`reject_tune`

```json
{
  "command": "reject_tune"
}
```

### 5.7 Calibration

`temp_calibration`

```json
{
  "command": "temp_calibration",
  "tempOffsetC": 0.4,
  "tempSmoothingAlpha": 0.25
}
```

Ranges:

- `tempOffsetC`: constrained to `-10.0` to `10.0`
- `tempSmoothingAlpha`: constrained to `0.0` to `1.0`

`calibration_status`

```json
{
  "command": "calibration_status"
}
```

### 5.8 Readback / query

`get_config`

```json
{
  "command": "get_config"
}
```

`get_events`

```json
{
  "command": "get_events"
}
```

### 5.9 Profile commands

`profile_select`

```json
{
  "command": "profile_select",
  "index": 0
}
```

`profile_start`

```json
{
  "command": "profile_start",
  "index": 0
}
```

`profile_delete`

```json
{
  "command": "profile_delete",
  "index": 0
}
```

`profile_upsert`

```json
{
  "command": "profile_upsert",
  "profile": {
    "index": 0,
    "name": "MASH",
    "stages": [
      {
        "name": "STEP1",
        "targetC": 65.0,
        "holdSeconds": 1800
      },
      {
        "name": "STEP2",
        "targetC": 72.0,
        "holdSeconds": 1200
      }
    ]
  }
}
```

Rules:

- `index` is optional. If omitted, a new profile is appended.
- Maximum profiles: `6`
- Maximum stages per profile: `8`
- Valid `targetC`: `20.0` to `120.0`
- At least one valid stage must remain after validation

## 6. Integration / controller-only topics

These are used by the integrated controller path rather than normal runtime control.

### 6.1 Device publishes enrollment request

Topic:

```text
<base>/enrollment/request
```

Payload:

```json
{
  "type": "enrollment_request",
  "system_id": "system_a",
  "controller_id": "controller_a",
  "device_id": "pid_01",
  "device_type": "thermal_controller",
  "firmware_version": "v0.9.0-dev",
  "controller_fingerprint": "...",
  "epoch": 1,
  "issued_at": 123456
}
```

### 6.2 Controller publishes enrollment response

Topic:

```text
<base>/enrollment/response
```

Payload:

```json
{
  "accepted": true,
  "system_id": "system_a",
  "controller_id": "controller_a",
  "controller_fingerprint": "..."
}
```

Requirements:

- `accepted` must be `true`
- `system_id` must match the device bootstrap metadata
- `controller_id` must match the device bootstrap metadata

### 6.3 Controller heartbeat

Topic:

```text
<base>/controller/heartbeat
```

Payload:

```json
{
  "controller_id": "controller_a"
}
```

Notes:

- Device supervision timeout is currently `20s`.
- If heartbeat or MQTT link is lost while integrated, disconnect fallback is applied.

### 6.4 Development-only integration helpers

These exist in firmware now and may be useful during development, but they are not the long-term production controller API.

`pairing_mode`

```json
Topic: <base>/cmd/pairing_mode
Payload: {}
```

`bootstrap_inject`

```json
Topic: <base>/cmd/bootstrap_inject
Payload:
{
  "version": 1,
  "system_id": "system_a",
  "system_name": "System A",
  "controller_id": "controller_a",
  "controller_public_key": "dev-key",
  "ap_ssid": "project6",
  "ap_psk": "sIlver@99",
  "broker_host": "10.42.0.1",
  "broker_port": 1883,
  "issued_at": 123456,
  "epoch": 1,
  "signature": "dev-allow"
}
```

`unpair`

```json
Topic: <base>/cmd/unpair
Payload: {}
```

## 7. Published payloads

### 7.1 Status

Topic:

```text
<base>/status
```

Retained: yes

Key fields:

```json
{
  "_type": "status",
  "tempC": 27.3,
  "rawTempC": 27.5,
  "sensor_mode": "single",
  "probe_count": 1,
  "probe_a_temp": 27.3,
  "probe_b_temp": null,
  "probe_a_ok": true,
  "probe_b_ok": false,
  "feed_forward_enabled": false,
  "operating_mode": "integrated",
  "integration_state": 2,
  "paired_metadata_present": true,
  "controller_enrollment_pending": false,
  "controller_connected": true,
  "integrated_fallback_active": false,
  "testingModeEnabled": true,
  "control_authority": "controller",
  "control_enabled": true,
  "system_id": "system_a",
  "system_name": "System A",
  "controller_id": "controller_a",
  "setpointC": 65.0,
  "heaterOutputPct": 32.4,
  "heaterEnabled": true,
  "controlMode": 1,
  "runState": 1,
  "autoTunePhase": 0,
  "pidKp": 18.0,
  "pidKi": 0.08,
  "pidKd": 20.0,
  "sensorHealthy": true,
  "low_temp_alarm_active": false,
  "high_temp_alarm_active": false,
  "wifiConnected": true,
  "mqttConnected": true,
  "alarmCode": 0,
  "alarmAcknowledged": false,
  "alarmText": "OK",
  "activeStage": "",
  "remainingSec": 0
}
```

Useful enum values in status:

- `runState`
  - `0` idle
  - `1` running
  - `2` paused
  - `3` complete
  - `4` fault
  - `5` autotune
- `controlMode`
  - `0` local
  - `1` remote
- `integration_state`
  - `0` none
  - `1` bootstrap pending
  - `2` enrolled

### 7.2 Shadow

Topic:

```text
<base>/shadow
```

Retained: yes

Payload:

```json
{
  "_type": "shadow",
  "desired": {
    "setpointC": 65.0,
    "minutes": 60,
    "runAction": "start"
  },
  "reported": {
    "runState": "running",
    "setpointC": 65.0,
    "effectiveTimerSec": 3520,
    "stageTimerStarted": true
  }
}
```

### 7.3 Config

Topic:

```text
<base>/config
```

Retained: yes for config snapshots, not retained for command acks

Config snapshot payload:

```json
{
  "_type": "config_effective",
  "controlLock": 2,
  "systemId": "system_a",
  "deviceId": "pid_01",
  "deviceClass": "pid",
  "operatingMode": "integrated",
  "integrationState": 2,
  "pairedMetadataPresent": true,
  "controllerEnrollmentPending": false,
  "controllerConnected": true,
  "integratedFallbackActive": false,
  "controlAuthority": "controller",
  "controlEnabled": true,
  "testingModeEnabled": true,
  "localAuthorityOverride": false,
  "systemName": "System A",
  "controllerId": "controller_a",
  "localSetpointC": 65.0,
  "minSetpointC": 20.0,
  "maxSetpointC": 99.0,
  "manualStageMinutes": 60,
  "overTempC": 99.0,
  "tempAlarmEnabled": true,
  "lowAlarmC": 18.0,
  "highAlarmC": 85.0,
  "alarmHysteresisC": 1.0,
  "alarmEnableSensorFault": false,
  "alarmEnableOverTemp": false,
  "alarmEnableHeatingIneffective": false,
  "alarmEnableMqttOffline": false,
  "alarmEnableLowProcessTemp": false,
  "alarmEnableHighProcessTemp": false,
  "mqttHost": "10.42.0.1",
  "mqttPort": 1883,
  "mqttUseTls": false,
  "mqttCommsTimeoutSec": 30,
  "mqttFallbackMode": 0,
  "wifiPortalTimeoutSec": 180,
  "profileCount": 0,
  "activeProfileIndex": 0,
  "pidKp": 18.0,
  "pidKi": 0.08,
  "pidKd": 20.0,
  "pidDirection": 0,
  "maxOutputPercent": 100.0,
  "activePidKp": 18.0,
  "activePidKi": 0.08,
  "activePidKd": 20.0,
  "autoTuneQualityScore": 0.0,
  "displayBrightness": 128,
  "buzzerEnabled": true
}
```

### 7.4 Events

Topic:

```text
<base>/events
```

Retained: no

Payload:

```json
{
  "_type": "event_log",
  "count": 3,
  "events": [
    { "atMs": 1200, "text": "System booted" },
    { "atMs": 3500, "text": "Run started" }
  ]
}
```

### 7.5 Calibration

Topic:

```text
<base>/calibration
```

Retained: yes

Payload:

```json
{
  "_type": "calibration_status",
  "tempOffsetC": 0.0,
  "tempSmoothingAlpha": 0.25,
  "tempC": 27.3,
  "rawTempC": 27.5,
  "sensor_mode": "single",
  "probe_count": 1,
  "probe_a_temp": 27.3,
  "probe_b_temp": null,
  "probe_a_ok": true,
  "probe_b_ok": false,
  "feed_forward_enabled": false,
  "sensorHealthy": true,
  "tempPlausible": true
}
```

### 7.6 Lifecycle

Topic:

```text
<base>/lifecycle
```

Retained: yes

Payload on profile completion:

```json
{
  "_type": "profile_complete",
  "value": true
}
```

## 8. Current testing mode overrides

When `testingModeEnabled` is true, the current firmware forces:

- Wi-Fi SSID: `project6`
- Wi-Fi PSK: `sIlver@99`
- MQTT host: `10.42.0.1`
- MQTT port: `1883`
- MQTT TLS: `false`
- MQTT auth: disabled

This override is runtime-only and does not erase stored integration metadata.

## 9. Node-RED implementation notes

Recommended minimum subscriptions:

- `<base>/status`
- `<base>/shadow`
- `<base>/config`
- `<base>/events`
- `<base>/lifecycle`

Recommended publish topics:

- `<base>/cmd`
- `<base>/enrollment/response`
- `<base>/controller/heartbeat`

Recommended controller behavior:

- publish a heartbeat faster than every `20s`
- watch `controller_connected` and `integrated_fallback_active`
- always inspect command ack `reason`
- treat `config_effective` as the authoritative effective runtime config snapshot
- use `profile_upsert` before `profile_start` if the controller is orchestrating staged runs
