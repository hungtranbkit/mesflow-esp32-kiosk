# ESP Kiosk v5.6.1 — M910 hardware adaptation

## Baseline and non-goals
- **Base source:** `bb256278b738041e989ada5f725121756c1ab7e8`, v5.6.0-KIMEX-MULTI-OP (the known-good Kiosk Linux lineage).
- **Stable Kiosk Linux device is NOT updated:** ESP MAC 28:84:85:85:75:10, binary at `/home/kiosk1/esp-update/fw-5.6.0/esp.ino.bin`; SHA256 `23051241889ad86839b2b7a551f9dbbedd76c1b7de218e212c3ad5f423757362`.
- This is **not a merge** of v5.5.12 logic. Preserve Multi-OP (up to six open operations), selection of active jobs, offline journal and replay, keypad recovery, multi-stage production flow, and runtime scanner baud auto-detection.
- Git branch: `adapt/v5.6.0-m910-gm65-20261110`.

## Hardware profile — M910
- ESP32-S3 N16R8, 16 MiB Flash + 8 MiB OPI PSRAM, 240x320 display.
- ESP MAC B8:1F:3F:C4:40:D0; M910 host, USB JTAG-Serial via `/dev/serial/by-id/`.
- GM65 TXD -> ESP RX on GPIO44, shared GND, 3.3 V logic. ESP UART TX unused.
- Scanner runtime baud starts 115200, can auto-detect from UART frame errors and saves verified baud in NVS. Do not override the v5.6.0 scanner state machine.
- Build FQBN: `esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=default_8MB,PSRAM=opi,CDCOnBoot=cdc`.

## Minimal source differences from base v5.6.0
1. Add trusted GTS Root R4 to permit authenticated HTTPS to MESFlow and Deploy Agent with no `setInsecure()` fallback.
2. Persist server-approved station to `mesflow_cfg` on successful bind and repair old on-device NVS station mismatch *only if an existing kiosk token is present*. No auto-approval or bypass of backend credentials.
3. Show actual `scannerBaud` in device health instead of hardcoded 9600.
4. Advance firmware label from v5.6.0 to v5.6.1-M910-COMPAT.

## Flash safety and validation
- Preserve the full current M910 flash, including current NVS, before installing any app.
- Verify the destination device MAC, flash/PSRAM capacities, and that there are no pending transactions, queued offline events, or in-progress operator workflow.
- Application-only flash at `0x10000`; do not rewrite NVS, partitions, bootloader, or erase all flash.
- Verify firmware version from the live device after reboot, MESFlow bind and station retention, scanner UART, and Multi-OP functionality.
- Do not flash the production Kiosk Linux ESP as part of this adaptation.
