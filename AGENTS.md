# esp-kiosk

Read and obey the workspace rules first:

`../AGENTS.md`

Project directory:
`esp-kiosk/`

Do not modify sibling projects unless the task explicitly requires a cross-project change.

## Auto detect + build + flash

Codex may automatically detect, compile and flash the ESP kiosk firmware.

Rules:
- Run board detection first.
- Automatically flash only when exactly ONE compatible ESP32-S3 serial port is detected.
- If zero or multiple compatible ports are found, STOP and report the candidates.
- Never choose a port silently when ambiguous.
- Never erase flash unless explicitly requested.
- Never change pin mapping, partition scheme, PSRAM/flash settings, or FQBN merely to make a build pass.
- Read `.mesflow-arduino.env` for the approved FQBN/options.
- Compile before every upload.
- After flash, open serial monitor and verify boot/app version.
