# ESP Kiosk ES3C28P / GM65 compatibility (2026-11-10)

## Versions and safe rollback

- Original firmware v5.5.7 is preserved at commit `92a80d4ce6ce3c5f169a7d7798ed2309c856b085`.
- Remote backup branch: `backup/esp-kiosk-v5.5.7-20261110`.
- New candidate: `v5.5.8`, branch `fix/esp-kiosk-hardware-gm65-20261110`.
- Original compiled v5.5.7 image is backed up on Mac Linux under `/home/mac/workspace/esp-kiosk-tools/backups/v5.5.7/`.
- Do not erase/flash an existing device until the actual chip/flash/PSRAM configuration is verified and its flash is backed up.

## Hardware identification

- LCDWiki ES3C28P is a 2.8-inch 240x320 ILI9341 touchscreen ESP32-S3 board specified with 16 MB external flash and 8 MB OPI PSRAM.
- M910's detected USB device currently advertises `ESP32-S3-DevKitC-1-N8 (8 MB, No PSRAM)`, serial `B81F3FC440D0`. A USB product string alone is not enough to conclude that the physical board differs from the ES3C28P. Read actual flash ID and PSRAM diagnostics before flashing.
- The connected USB interface has not yet entered ROM download mode reliably; esptool returned `No serial data received` with default reset. Local BOOT+RESET may be necessary for read-only verification.

## GM65 wiring and baud

- Connect scanner TXD to ESP UART RXD / GPIO44, and connect common GND. Legacy scanner UART is RX-only (`ScannerSerial.begin(..., RX=44, TX=-1)`); no ESP TX wire is required.
- ESP32-S3 GPIO signal levels are 3.3 V. The scanner's power pin marked 5V does not prove that its TXD pin is safe at 3.3 V; measure/check level shifting.
- This legacy unit was physically tested at **115200 baud** (13 of 13 exact reads). Trying the factory-default 9600 baud on that unit yielded no bytes; therefore **115200 remains the firmware default**.
- v5.5.8 adds the optional compiler flag `-DMESFLOW_GM65_BAUD=9600` for GM65 units configured to 9600; permitted values are 9600 or 115200.

## Build and release

- Arduino board FQBN: `esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=default_8MB,PSRAM=opi,CDCOnBoot=cdc`.
- The default 115200 variant builds with ordinary `arduino-cli compile --fqbn <FQBN> <sketch>`.
- The 9600 variant adds `--build-property compiler.cpp.extra_flags=-DMESFLOW_GM65_BAUD=9600`.
- Both v5.5.8 variants compiled successfully on the Mac Linux machine using ESP32 Arduino core 3.3.11.
- Compiled images are staged on Mac Linux under `/home/mac/workspace/esp-kiosk-tools/builds/compat115200/` and `compat9600/`.
- A successful build is **not** proof of having flashed, booted, or read any real QR scan.

## Flash gate

1. Identify the physical board and obtain actual flash capacity and PSRAM diagnostics.
2. Backup all existing flash/NVS and independently verify backup checksum/size.
3. Only then flash the matching, selected baud variant; verify upload and observe boot logs.
4. Test scanner, touchscreen, keypad, Wi-Fi, MES API, and rollback before production release.
