# ESP32 firmware profile matrix — 5.6.16 scanner safety follow-up

This candidate is based on canonical main commit `4d10dbc54763abfe312531834d904b31e7983cdd` (5.6.15). It does not change board hardware selection. The shared source adds duplicate QR suppression and a USB maintenance scanner probe; probe access is limited to the verified ES3C28P M910 profiles below. The full device identity/evidence matrix is in `FIRMWARE_PROFILE_MATRIX_5_6_15.md`.

| Profile | Existing verified hardware facts | Delivery | 5.6.16 source qualification |
|---|---|---|---|
| `es3c28p-n16r8-m910-gm65uart1` | ES3C28P, ESP32-S3 N16R8, ILI9341 SPI, GM65 UART1 RX GPIO44, 16 MiB flash, 8 MiB OPI PSRAM, `default_8MB`, dual app slots of 3,342,336 bytes | OTA candidate; requires exact-device identity and OTA auth/rollback gate | Local artifact/build results to be recorded after pinned build |
| `es3c28p-n16r8-m910-hugeapp-serial` | Same verified ES3C28P/M910 display and scanner wiring; `huge_app`, one 3 MiB app slot | Serial app only; no OTA/rollback slot | Local artifact/build results to be recorded after pinned build |
| Kimex Máy 1 / Máy 2, Kiosk Linux, JC4827W543 | See 5.6.15 matrix; board/partition/profile remains unknown or incompatible | Excluded from both packages | No image build or hardware write |

Version/build source identity: **5.6.16 / `20261128.0006`**. Build IDs include the exact source commit. No external CA, auth secret, token, Wi-Fi credential or NVS value is embedded. The source merge gate does not authorize a production flash; no current device has verified UUID + matching profile + idle state + backup + rollback.
