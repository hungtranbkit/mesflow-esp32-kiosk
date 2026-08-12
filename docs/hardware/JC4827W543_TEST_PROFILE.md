# JC4827W543 test profile (not production)

This board was used only for a display smoke test. Do not include
`Arduino_GFX_Library` or this profile in the production kiosk build.

- MCU: ESP32-S3-WROOM-1-N4R8 (4 MB flash, 8 MB OPI PSRAM)
- LCD: NV3041A, QSPI, 480x272
- Backlight: GPIO 1, active high
- QSPI: CS 45, SCK 47, D0 21, D1 48, D2 40, D3 39
- Arduino display object:

```cpp
Arduino_ESP32QSPI bus(45, 47, 21, 48, 40, 39);
Arduino_NV3041A panel(&bus, GFX_NOT_DEFINED, 1, true);
```

- Board options: `FlashSize=4M`, `PartitionScheme=huge_app`, `PSRAM=opi`
- Verified test device: COM5 at test time, MAC `a4:cb:8f:ec:1d:9c`

Source: https://github.com/lsdlsd88/JC4827W543
