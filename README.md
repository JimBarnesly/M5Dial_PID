# M5Dial HLT Controller V3

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

Known limitation:
- Settings icon is visual only in this build.


V4 notes:
- display layer rewritten to follow the Stitch screen much more closely
- static chrome is drawn once, with only changed regions refreshed
- ring follows the outer thin countdown layout from the Stitch mock
- bottom icons are rendered with primitives instead of text glyphs for runtime stability

## MQTT topic schema (Node-RED integration)

Base topic: `brew/hlt`

### Command topics (subscribe/publish from Node-RED to device)

- `brew/hlt/cmd/setpoint`  
  Payload example:
  ```json
  {"cmdId":"nr-1001","setpointC":67.5}
  ```
- `brew/hlt/cmd/minutes`  
  Payload example:
  ```json
  {"cmdId":"nr-1002","minutes":90}
  ```
- `brew/hlt/cmd/start`
- `brew/hlt/cmd/pause`
- `brew/hlt/cmd/stop`
- `brew/hlt/cmd/reset_alarm`
- `brew/hlt/cmd/start_autotune`
- `brew/hlt/cmd/accept_tune`

For action commands (no scalar value), a minimal payload with only `cmdId` is enough:
```json
{"cmdId":"nr-1003"}
```

### Command acknowledgement topic (device -> Node-RED)

- `brew/hlt/event/cmd_ack` (non-retained)

Payload schema:
```json
{
  "cmdId": "nr-1003",
  "command": "start",
  "accepted": true,
  "applied": false,
  "reason": "wrong_run_state",
  "reported": {
    "runState": "running",
    "setpointC": 67.5,
    "effectiveTimerSec": 5100
  }
}
```

`reason` values include:
- `applied`
- `invalid_json`
- `unsupported_command`
- `control_lock_local_only`
- `invalid_range_setpoint`
- `invalid_range_minutes`
- `wrong_run_state`

### Device shadow topic (device -> Node-RED)

- `brew/hlt/shadow` (retained)

Payload schema:
```json
{
  "desired": {
    "setpointC": 67.5,
    "minutes": 90,
    "runAction": "start"
  },
  "reported": {
    "runState": "running",
    "setpointC": 67.5,
    "effectiveTimerSec": 5100,
    "stageTimerStarted": true
  }
}
```

Shadow intent:
- `desired.*` is the latest requested operator intent from remote commands.
- `reported.*` is what the controller is currently doing at runtime.

### Existing status topic (device -> Node-RED)

- `brew/hlt/status` (retained, full telemetry snapshot)
