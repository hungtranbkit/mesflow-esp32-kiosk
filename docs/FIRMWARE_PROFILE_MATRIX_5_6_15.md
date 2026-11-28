# ESP32 firmware profile matrix — merged main 5.6.15 baseline

Canonical source: `main` at merge commit `4d10dbc54763abfe312531834d904b31e7983cdd`, merged from PR #9. Firmware source is shared; only hardware-specific build profiles may select verified pins, flash/partition layout, scanner defaults or display drivers. An unknown profile stays inactive and must not enter release artifacts. A matching model name is not physical identity. The 5.6.16 scanner-safety follow-up is documented separately in `FIRMWARE_PROFILE_MATRIX_5_6_16.md`.

## Evidence-backed hardware matrix

| Device/profile | Evidence and identity | Display / pins | Flash / partition / OTA | Release decision |
|---|---|---|---|---|
| Kimex Máy 1, UUID `fc0c33a9-2f76-424f-a256-217699b57712` | Inventory/heartbeat reports ES3C28P and 5.6.6; no confirmed MAC-to-UUID join | LCD wiring and scanner baud not tied to UUID | Flash size, partition table and slot count unknown | No profile/image assigned; no write |
| Kimex Máy 2/A, UUID `0f2d2ad3-06d7-417f-a1e7-da3d2457db39`, MAC `84:C7:BB:79:39:84` | Accepted read-only hardware receipt and verified table in `M910_HUGE_APP_SERIAL.md` | ES3C28P; ILI9341 240x320; GM65 TX→GPIO44/RX, ESP TX unused; runtime scanner baud is NVS/auto-detected and must not be assumed from product label | 16 MiB flash, 8 MiB OPI PSRAM; `huge_app`, app0 at `0x10000`, 3 MiB; partition SHA `aaae2888c5a6a348004b5b436f47abb25ae32e72d9003902955a998eda723edd`; one app slot, no OTA rollback | `es3c28p-n16r8-m910-hugeapp-serial`; serial app only, preserve NVS/partition and retain verified app backup for manual rollback |
| M910 dual-slot reference profile | Hardware/build evidence in `M910_V560_COMPAT.md` and CI profile; not joined to a current UUID | ES3C28P; ILI9341 240x320; GM65 UART1 RX GPIO44/TX unused; historical unit has tested 115200; a different module showed 9600/no bytes; firmware can persist/detect baud | 16 MiB flash, 8 MiB OPI PSRAM; `default_8MB`, dual app slots 3,342,336 bytes; partition SHA `1d9cca96de0fe07ad7fc0648b9878ddecd9ce565e38b589ad20fea698ed4c80c` | `es3c28p-n16r8-m910-gm65uart1` is the only OTA profile in this matrix; buildability does not identify a device |
| Historical M910 UUID `2eebb378-e0dd-4c3b-aa4d-39ae012437e0` | Inventory last seen 2026-11-11; reported 5.6.2, not tied to the incident | Physical MAC, LCD and scanner settings not freshly joined | Actual partition layout unknown; both dual-slot and single-slot M910 profiles exist | Do not assign either image without fresh UUID/MAC/partition evidence |
| Dell kiosk1 USB reference, MAC `28:84:85:85:75:10` | Prior USB metadata and Kiosk Linux notes; no incident UUID join | ESP32-S3 candidate only; deployed image and scanner wiring not proven | Existing profile is marked `UNVERIFIED`; no verified partition receipt | Builder skips it; no serial port opened and no image assigned |
| JC4827W543 display test board, MAC `a4:cb:8f:ec:1d:9c` | `hardware/JC4827W543_TEST_PROFILE.md`; test-only evidence, not a Kimex kiosk | ESP32-S3-WROOM-1-N4R8; NV3041A 480x272 QSPI; BL GPIO1; QSPI CS45/SCK47/D0=21/D1=48/D2=40/D3=39. Current production source uses ILI9341 SPI and does not select this controller/pin map | 4 MiB flash, 8 MiB OPI PSRAM; documented `huge_app` test setting, but no matching verified product profile/OTA slot artifact | Keep test-only/inactive; do not build or flash a production image for it from the ES3C28P profile |

The M910 serial-only profile and dual-slot profile are distinct and cannot be substituted. The Kiosk Linux, Kimex Máy 1 and JC4827W543 profiles remain excluded from release packages. No current device has been established as an idle, rollback-capable pilot target; this document does not authorize hardware writes.

## Release artifact state

- Main source version: **5.6.15**, build `20261128.0005`, merged by PR #9. This source merge does not authorize a hardware write.
- The previous 5.6.14 artifacts are retained as superseded evidence under `task-artifacts/po6391-diagnosis-20261128/firmware-canonical-5.6.14/`; they do not contain the current credential persistence or OTA ACK/logging fixes and must not be promoted as 5.6.15.
- Dell pinned build at code commit `bf1cf9cbb18b2f0333ad40c09bf171b46fb0c130` (ESP32 core 3.3.11, GFX 1.13.0, ILI9341 1.7.0, BusIO 1.17.4, ArduinoJson 6.21.5):

| Profile | App / capacity | App SHA-256 | Package SHA-256 | Delivery |
|---|---:|---|---|---|
| `es3c28p-n16r8-m910-gm65uart1` | 1,448,064 / 3,342,336 bytes | `5ebe988f07dcd739b6f577c85e934b9a09555b3da07c3d920b716d7959146b8b` | OTA ZIP `2cfcc0a28ef82b540f9fe0ed31ee8ea4e72b0107079b12c622372724c6fb9be7`; legacy ZIP `67a3c9bba21c85278e526b59b6963c58df2d4fe568ab40259aa44c04eadaa91f` | OTA dual-slot; verifier PASS |
| `es3c28p-n16r8-m910-hugeapp-serial` | 1,448,064 / 3,145,728 bytes | `1a0318533c841987de7865231aa77e9276e2cb4b73df96456c9e7194d519494a` | none by design | app-only, manifest `ota_capable=false` |

Artifacts/manifests/checksums are retained at `/home/dell/workspace/task-artifacts/po6391-diagnosis-20261128/firmware-canonical-5.6.15-bf1cf9c/` as pre-merge local evidence. Exact merged-main CI artifacts take precedence for promotion. Built-in GTS Root R4 only; no external OTA CA/auth secret, device token, Wi-Fi credential, UUID secret or NVS data is embedded. The builder skips unverified profiles.

## Scanner baud evidence

The source supports build-time `MESFLOW_GM65_BAUD` values 9600/115200 and runtime persisted baud/auto-detection. The older hardware branch's optional 9600 compiler flag is already represented in current source; there is no evidence to set it for Kimex Máy 1, Máy 2, Kiosk Linux or JC4827. The GM65 manual default and USB-receiver baud are not evidence of the module's stored baud. See `M910_V560_COMPAT.md` and `hardware/GM65_SETUP_REUSE.md`.
