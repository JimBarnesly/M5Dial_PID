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
