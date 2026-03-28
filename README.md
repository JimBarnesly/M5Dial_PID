# M5Dial Brew HLT Controller

PlatformIO project for an M5Dial-based HLT controller with:

- DS18B20 on GPIO 13
- SSR via MOSFET on GPIO 2
- Local PID control using time-proportional output
- Local / remote MQTT control modes
- Multiple named stage profiles in flash
- Captive portal Wi-Fi provisioning
- Stage-complete event buffering until MQTT reconnects
- UI based on the provided Stitch circular layout

## Current state

This is a solid v1 firmware base, but there are still hardware-specific items you should validate before flashing:

1. **M5Dial buzzer pin**
   - `PIN_BUZZER` is set to `3` as a common default.
   - Confirm against your actual board pinout.

2. **Center button / encoder APIs**
   - This project uses `M5.BtnA` and `M5.Dial.getCount()`.
   - If your installed M5Unified version exposes these differently, adjust `processInput()`.

3. **Float switch**
   - It is assumed to be hardwired in series with the SSR input for safe cutout.
   - For explicit low-level alarm display, also wire it to a GPIO and add firmware support.

4. **Settings menu**
   - The main screen settings icon is currently a placeholder trigger.
   - The core control logic is in place; menu pages can be added cleanly.

## MQTT topics

Base topic: `brew/hlt`

### Publish
- `brew/hlt/status`
- `brew/hlt/event/profile_complete`

### Subscribe
- `brew/hlt/cmd/setpoint`
- `brew/hlt/cmd/control_lock`
- `brew/hlt/cmd/mode`
- `brew/hlt/cmd/profile`
- `brew/hlt/cmd/profile_json`
- `brew/hlt/cmd/start`
- `brew/hlt/cmd/pause`
- `brew/hlt/cmd/stop`
- `brew/hlt/cmd/reset_alarm`

## Next practical steps

- Confirm buzzer pin and any M5Dial API differences
- Add a proper settings screen stack
- Add direct float-switch GPIO feedback
- Add optional second sensor / independent overtemp safety
