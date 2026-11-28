# PROJECT_CONTEXT — esp-kiosk (legacy ESP32-S3 kiosk, "v1")

Living handoff for the next agent. Code/tests/git/runtime evidence win over these notes.

## Integrated scanner + Wi-Fi setup cancellation candidate — 5.6.11 (2026-11-28)

- Worktree/branch: `/home/dell/workspace/mesflow/.worktrees/esp-po6391-v5611-integration`, `agent/codex/esp-po6391-v5611-integration-20261128`.
  Source baseline is PR #4 merge `c0a80ce3d3c8888ffd07ddce5e175eed2636eeb4` on
  `fix/deepsleep-ota-gate-20261124`, not `main`/92a80d4/5.5.7. Scanner PR #6
  (`fix/scanner-utf8-v5610-20261128`, head `ad37cba`) carries the UTF-8 QR
  decoder, `WF|OPID|` support and host tests. This branch adds Wi-Fi cancel
  commit `3ad628d` (port of `81bb5f6`) and version/test commit `b18cc4b`.
  Draft stacked PR #8 targets PR #6's branch; PR #6 must merge first.
- Candidate source identity is 5.6.11 / `20261128.0001`; release builder
  appends the exact source SHA to build ID `20261128.0001.gb18cc4bb7d`.
  Scanner keeps UTF-8 bytes and the pre-existing printable ASCII ratio used
  for rejected-frame baud recovery; the local worker-required guard remains.
  Wi-Fi cancellation is non-destructive, scoped to physical `#` or the active
  setup AP nonce, and gated against active workflow/OTA. It does not reboot or
  write NVS/config/journal; same-SSID password preservation is explicit and
  stored Wi-Fi password is not rendered. Setup/sleep/OTA share a tested gate.
- Verified local release artifact is serial-only M910 huge_app:
  `/home/dell/workspace/task-artifacts/po6391-diagnosis-20261128/firmware-v5611-m910-serial/`.
  App size 1,445,504 bytes / 3,145,728-byte app0; app SHA256
  `e46c5bd41d3ee8db984d43ead663f315966eb09c366c86240c3e38c7cad9c835`;
  manifest SHA256 `50efa235e401e13ad59b6f76842a0df0ffc5b1e2f2fba676c1a526e772a1eb4`.
  Partition SHA matches the verified huge_app table. Manifest declares
  `delivery=serial-app-only`, `ota_capable=false`; no OTA ZIP was created.
- Tests on the integrated source before the doc-only handoff commit: 61 Python
  tests pass, all eight C++ host suites pass (`-Wall -Wextra -Werror`), including
  1,000 threaded OTA/sleep/setup race interleavings. The Wi-Fi regression failed
  on baseline c0a80ce (6 failed, 1 passed) and passed after the patch. The
  serial-only build/manifest verifier passed. Real hardware cancel, physical
  target attribution and rollback on the candidate were not tested.
- Available latest physical evidence is historical: A / Máy 2 reported 5.6.10
  on 2026-11-27; B / Máy 1 reported 5.6.6. PO6391 target firmware remains
  unverified. Dell USB reference MAC `28:84:85:85:75:10` is not A's accepted
  M910 MAC; no serial session, reset, flash, OTA, production mutation, merge,
  or deployment was performed. Do not treat this serial-only artifact as OTA
  eligible. Require exact target/profile and authorized rollback qualification.
- Separate continuation handoff: `/home/dell/workspace/task-artifacts/po6391-diagnosis-20261128/firmware-integrated-v5611-handoff.md`.

## Current change — numeric display IDs, firmware 5.6.10 (2026-11-27)

- Owned branch: agent/codex/esp-display-number-v5610-20261127.
  Worktree: /home/dell/workspace/mesflow/.worktrees/codex-esp-display-number-20261127.
  Base: merged d425c8949fc9da113bd1b491522f39609752259c, whose tree matches the
  accepted 5.6.9 source 813df3e065651584d1f9c3263e6591cabd1b8c32.
- User requested short IDs: Máy 1 = 01, Máy 2 = 02. MESFlow now owns optional
  authenticated response field display_number, a JSON integer 1..2147483647.
  The firmware uses minimum two decimal digits; 100+ never wraps or truncates.
  UUID/secret/token/station/enrollment challenge are not changed.
- esp/mesflow_device_identity.h contains the display policy and versioned
  240-byte cache schema. esp/mesflow_app.cpp accepts the value after successful
  bind or a valid successful heartbeat, writes/reads back one NVS blob at
  mesflow_cfg/display_num, and publishes only after persistence succeeds.
  Cache scope is normalized configured server plus exact full UUID.
  Missing/invalid fields retain a same-server cache; no usable cache shows ID --.
  Equal values do not write flash or request a repaint. See docs/NUMERIC_DISPLAY_ID.md.
- Header changes reuse the existing deferred idle refresh, preserving active
  employee/operation/quantity screens and OTA/recovery/maintenance guards.
  The 36px header, Vietnamese name rendering, clock and network placement remain.
  GET /api/device/info includes display_number only when a scoped value exists.
- Verification: the actual old header first failed the new ID02 regression.
  The real NVS/intake/header adapter, reboot and scope cases now pass.
  Established source gates pass: 53 Python tests and all eight C++ host
  executables with -Wall -Wextra -Werror. Actual-font tests cover ID --, 01, 02,
  100 and 2147483647. An external one-off probe using installed ArduinoJson
  6.21.5 passes all ten requested integer/type/range fixtures.
- Firmware build 20261127.0004 is prepared, not compiled or flashed yet.
  Next: review the clean source, compile the pinned serial-only profile once,
  freeze its manifest/digest and review the exact target before physical action.
  No source/profile/security/physical gate is bypassed.
  Evidence: /home/dell/workspace/mesflow/artifacts/esp-display-number-20261127.

## Accepted hardware baseline — A on firmware 5.6.9, B on 5.6.6

- A: MAC 84:C7:BB:79:39:84, UUID 0f2d2ad3-06d7-417f-a1e7-da3d2457db39.
  Accepted app SHA 69380ada8dfb7ab502d0cf8d7356a338806b4f72416d8cf4a1f5914327b4c69b,
  size 1427952, source 813df3e065. App-only write and app/protected-prefix/suffix
  verification passed before one watchdog-reset boot at 2026-11-27T07:11:40Z.
  LAN/LCD and production web acceptance passed: Máy 2, then-current ID 2457DB39,
  normal SCAN, bound/online READY, empty worker/operation, zero queues.
  Name arrived automatically through heartbeat, without a UI reset.
- B: MAC 84:C7:BB:79:39:6C, UUID fc0c33a9-2f76-424f-a256-217699b57712.
  Web name is Máy 1; existing firmware 5.6.6 remains unmodified.
  No B USB target is currently available. Do not claim B has the new LCD.
- A's actual verified table is Arduino 3.3.11 huge_app, one 3 MiB app and no
  inactive OTA slot; table SHA256:
  aaae2888c5a6a348004b5b436f47abb25ae32e72d9003902955a998eda723edd.
  Use explicit VERIFIED profile es3c28p-n16r8-m910-hugeapp-serial and
  scripts/build-serial-release.sh. App offset 0x10000 / capacity 0x300000.
  No OTA ZIP, bootloader/table/NVS/FS replacement, or generic flash.sh upload.
  See docs/M910_HUGE_APP_SERIAL.md. Default dual-slot OTA profiles are unchanged.
- Exact source lineage retains the A sleep/OTA interlock and B keypad/scanner
  features: ECD gated source ecd93e0f092cff13c9dc5dad8ea5fcc9e41040d6 has the
  same tree as b88928ec251c17ed0e86bf38d05989b4cc119025; it descends from
  356b4dbd02 sleep work and 2b131c1354 keypad work. Embedded old 5.6.6 labels
  were pre-commit build IDs, not sufficient binary provenance.
- Preserve accepted 5.6.9 and unflashed retired 5.6.7 / 5.6.8 release bytes.
  Pin ESP32 core 3.3.11, GFX 1.13.0, ILI9341 1.7.0, BusIO 1.17.4, ArduinoJson 6.21.5.
  Artifacts/backups and final 5.6.9 receipt remain outside the source repo under
  /home/dell/workspace/mesflow/artifacts/esp-display-identity-20261127/v569.
- Runtime OTA capability and installation both require a distinct inactive
  partition. Credential-save/readback precedes approval display changes.
  Canonical names remain nonblank UTF-8 at most 63 bytes without C0/DEL.
  No pending display work means no extra LittleFS journal reads.
- On this A board, RTS hard-reset can leave ROM DOWNLOAD active. Use only
  the already verified installed esptool 5.6.0 watchdog-reset boot procedure
  after the reviewed app/protected-byte verification. Preserve exclusive USB
  ownership and a fresh exact-identity/idle/backup plan for every physical trial.

## Historical reference state (2026-10-07; different Dell board)
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
