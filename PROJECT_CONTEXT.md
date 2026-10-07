# PROJECT_CONTEXT — esp-kiosk (legacy ESP32-S3 kiosk, "v1")

Living handoff for the next agent. Code/tests/git/runtime evidence win over these notes.

## Current state (2026-10-07)
- The reference board (ES3C28P, ESP32-S3 N16R8, MAC 28:84:85:85:75:10) on **dell `/dev/ttyACM0`** runs this
  firmware again, **FW 5.5.7 built from `main` 92a80d4 unchanged**, after the from-scratch rebuild
  `../mesflow-kiosk-runtime-v2` was judged too error-prone (user decision: back to v1; keep the ESP lean —
  few features, stable connection first).
- It talks to **DEV `http://dev.mesflow.net`** (container `mesflow-dev-app` on dell, schema 72.0.11, server_role DEV).
  Verified: heartbeat every ~20 s, `kiosk_status` READY/OK, RSSI −29…−38 dBm, free heap ~218 KB, firmware 5.5.7.
- Branch/worktree for this work: `agent/claude/v1-dev-reconnect` in `../.worktrees/claude-esp-kiosk-v1-dev`
  (no firmware code change was needed to connect).

## How the board was put back on v1 + DEV (no code change)
1. Backup before overwrite: `~/esp-backups/` (dell) — boot+partition table+**NVS** (`v2-0000-10000_boot_pt_nvs_otadata.bin`)
   and most of the v2 app; full 16 MB read keeps failing over the native USB-JTAG ("Packet content transfer
   stopped") — read in ≤1 MB chunks; v2 can be rebuilt from source anyway.
2. `./scripts/build.sh` + `echo y | ./scripts/flash.sh /dev/ttyACM0` (FQBN from `.mesflow-arduino.env`:
   `esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=default_8MB,PSRAM=opi`; same partition layout as v2).
3. v1 had **no config** on the board (v2 had wiped `mesflow_cfg`), and DEV refuses unknown kiosks
   (`MESFLOW_ALLOW_LEGACY_KIOSK_AUTOBIND=0`; an ACTIVE identity must also present its current token). So the
   identity was pre-provisioned:
   - generated `device_uuid`/`device_secret` in v1's format; on DEV ran `KioskRepository().register()` +
     `approve(id, None)` inside `mesflow-dev-app` → identity **id 2230, uuid `e5dedadc-ca1f-452e-ab03-0d14683bfcbf`,
     ACTIVE, station NULL** (DEV has no stations; web kiosks run without one).
   - wrote the NVS partition (0x9000, 0x5000) with `esp-idf-nvs-partition-gen` (venv `~/esp-backups/nvsgen-venv`):
     `mesflow_cfg` {wifi_ssid=Airport, wifi_pass (copied from the v2 NVS), server=http://dev.mesflow.net,
     device_id=ESP-KIOSK-DEV-01, device_name, station=LASER-01, cfg_ver=1}, `mf_identity` {device_uuid, device_secret,
     identity_ver=1}, `mesflow` {token, station=LASER-01}. Files (0600, secrets) in `~/esp-backups/v1-provision/`.
   - v1 loads the stored token only if `mesflow/station == STATION_CODE` (default `LASER-01`), then skips bind
     (`rt.bound=true`) and heartbeats to `/api/station/heartbeat` with `X-Kiosk-Token`.
4. Serial logs: this FQBN has **USB CDC on boot OFF**, so `Serial` goes to UART0 pins, not `/dev/ttyACM0` —
   the USB port is silent. Observe via DEV DB (`kiosk_identities`, `kiosk_status`) or the remote console.

## v5.6.0 (2026-10-07) — multi-OP + stability (see CHANGELOG_v5_6_0.md)
- `OpenOp openOps[MAX_OPEN_OPS=6]` next to `RuntimeData rt`; helpers before `offlineLookupWorker`
  (`findOpenOp` matches QR or `WF|OPID|<id>` → operation id, `mergeLocalOpenOps`, `selectOpenOp`,
  `enterAfterWorkerScan`, `eventAnswered`). `lookupQr(worker)` fills the list; the OP-scan branch selects or starts.
- Fixed: ghost local session after an online finish; catalog refresh every 60 s when empty; background sync errors
  hijacking the screen; OTA polling with no CA (http.begin failures every 12–25 s).
- Verified end-to-end on the board against DEV (NV005, DEV-OP1/DEV-OP2: sessions 17/18) — see the changelog.
- **Bench board currently runs a TEST build with `CDCOnBoot=cdc`** (USB serial works: send `WF|EMP|…`, `WF|OP|…`,
  digits, `status` over `/dev/ttyACM0` 115200; harness `/home/dell/aigw-probe/esp_harness.py`). The repo profile
  (`.mesflow-arduino.env`) is unchanged (CDC off) — decide before fleet builds whether CDC should be on.
- Not done (audit, keep lean): event log is never compacted (after ~250 cycles the 500-ACK scratch overflows →
  old events resent / "BO NHO OFFLINE DAY"); `countPendingOfflineEvents` scans the log every loop; unauthenticated
  LAN console (port 17892) incl. factory-reset; device_secret over plain HTTP. No QC / pause / setup-skip-qty
  (web kiosk: SETUP skips the quantity screens).

## Scanner + peripherals (2026-10-07, verified on the bench board)
- Scanner: the module is at **115200** (a 9600 setting read 2 bytes of a 12-byte card). Firmware now auto-detects the
  baud (garbage frame -> next candidate; first valid `WF|...` frame saved to NVS `mesflow_cfg/scan_baud`); manual
  override `scanner-baud <baud>`. Real scans confirmed (`WF|EMP|NV001`, `WF|OP|1111-KM-967-232006L-01-OP01/OP02`).
- Keypad PCF8574T @0x20: hot-plug probe every 3 s + recalibration when it (re)appears or an unknown pair is pressed;
  the user calibrated it on the device (mapping in NVS) and used it to finish sessions #20/#21.
- "CÓ LỖI SỬA ĐƯỢC?" (only when defect > 0): `#` = KHÔNG -> confirm, `1` = CÓ -> repairable qty. Verified:
  #22 (4/1/1, CÓ) and #23 (3/2/0, KHÔNG).
- Touch FT6336G (0x38) is still not found on I2C (only 0x18 besides the keypad) — touch is optional; not investigated.
- Event log compaction added (>= 48 KB and fully answered -> deleted).

## Known gaps / next
- The transient `ui_state=ERROR` (08:02:31) matched background-sync `setError` calls hijacking the screen
  (audit finding) — now logged only (v5.6.0). DEV does have catalog data (27 workers / 319 operations).
- Feature audit vs Kiosk Web done (2026-10-07): web kiosk F1–F12 incl. multi-OP, SETUP skips quantities, error
  screen with server message/action, version auto-reload; ESP lacked multi-OP (done in 5.6.0). Remaining gaps to
  consider, keeping the ESP lean: LAN console auth (the provision token is
  broadcast over UDP, so it protects nothing — decide with the deploy-agent owners).
- To roll back to v2: rebuild `../mesflow-kiosk-runtime-v2` (`scripts/build-dev.sh`) and flash; its NVS keys
  (`kiosk_v2`, `kiosk_identity`) are in the backup above.
- Decision (user, 2026-10-07): SETUP operations keep the normal flow on the ESP (operators enter time
  there) -- do NOT add a "skip quantity screens" path like the web kiosk has.
