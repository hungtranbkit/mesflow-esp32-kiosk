# ESP Kiosk project context

## Current canonical candidate

- Branch/worktree: `integration/esp-canonical-release-20261128` at `/home/dell/workspace/mesflow/.worktrees/integration-esp-canonical-release-20261128`; head `dcd1deb05ba90eb1227f24fb1e2c4ce5a3a65f2a`.
- Draft PR #9 targets `main`: https://github.com/hungtranbkit/mesflow-esp32-kiosk/pull/9. PR state OPEN, mergeable, host-tests and build-release PASS on this head; review decision is still pending, so do not merge or mark ready.
- Firmware candidate `5.6.14`, build `20261128.0004.gdcd1deb05b`. This is candidate source identity, not evidence of a production device's installed version.
- The branch is based on current `main` `92a80d4`, integrates PR #8's 5.6.11 lineage (PR #4 numeric ID/approval/OTA/deep-sleep, PR #6 UTF-8/OPID scanner, PR #7 Wi-Fi cancel), and carries verified three-network Wi-Fi behavior from PR #1. PRs #5/#6/#7/#8 remain legacy/stacked; PR #9 is the canonical direct-to-main path.

## Preserved behavior and current code

- Scanner UTF-8 byte policy and `WF|OPID|` remain; GM65 CR/LF now terminates individual frames so queued scans do not merge. `WORKER_REQUIRED` still rejects operation scans before lookup. Worker `*` cancel clears only the local selection; open durable/server sessions remain.
- Multi-OP selection and offline queues remain. OPID matching is applied to offline sessions where the saved QR itself carries the numeric ID; server session IDs match scanned OPID. Offline FINISH records still pending ACK are hidden from server-open list. Startup recovers an action-queue tmp file when the old file was removed during an interrupted replacement.
- Wi-Fi setup cancel stays non-destructive and resumes primary asynchronously. Up to three SSIDs are stored in `mesflow_cfg`; config paths preserve UUID/token/approval/server/station namespaces. Deep-sleep, OTA idle interlock, inactive-slot/hash validation, boot confirmation and rollback protections remain.
- Numeric kiosk ID, approval/identity, QR GM65 UART1 and keypad/PCF8574 behavior remain. No schema/config migration was introduced.
- Main code change: `esp/mesflow_app.cpp`; Wi-Fi failover policy: `esp/mesflow_wifi_failover_policy.h`; release profile matrix: `docs/FIRMWARE_PROFILE_MATRIX_5_6_14.md`; lineage regression: `tools/test_canonical_lineage_contract.py`.

## Verified profile / target matrix

- Kimex Máy 1 UUID `fc0c33a9-2f76-424f-a256-217699b57712`: reported ES3C28P/5.6.6; exact MAC, LCD, flash and partition table are not joined. No image assigned.
- Kimex Máy 2/A UUID `0f2d2ad3-06d7-417f-a1e7-da3d2457db39`, MAC `84:C7:BB:79:39:84`: verified `huge_app` single app0 profile, 3 MiB at `0x10000`, partition SHA `aaae2888c5a6a348004b5b436f47abb25ae32e72d9003902955a998eda723edd`; serial-only, not OTA-capable.
- Historical M910 UUID `2eebb378-e0dd-4c3b-aa4d-39ae012437e0`: last seen Nov 11, reported 5.6.2; current physical identity/table is unverified. The repository has distinct verified dual-slot and single-slot build profiles; neither proves the physical unit's current layout.
- Dell USB reference MAC `28:84:85:85:75:10` remains an unverified Kiosk Linux profile; serial was not opened.
- No PO6391 physical UUID, idle state, current image backup or rollback target is proven. No device reset, flash, OTA, NVS erase, serial access or production write occurred. Do not target any unit from profile name alone.

## Build/test evidence and artifact location

- On source SHA `dcd1deb`: 10 C++ host suites passed (`-Wall -Wextra -Werror`, including 1,000 threaded OTA/sleep/setup interleavings); 69 Python tests passed.
- Pinned local build: ESP32 core 3.3.11, GFX 1.13.0, ILI9341 1.7.0, BusIO 1.17.4, ArduinoJson 6.21.5. OTA release verifier PASS.
- Dual-slot profile `es3c28p-n16r8-m910-gm65uart1`: app 1,440,096 bytes / slot 3,342,336; app SHA `717bf44df75a8e39176e56d94d2bc302adcdff976202b07ec17ae05a85a30de6`; OTA ZIP SHA `002ca40437db54b96888dadd00ae0eceb35cc917340262d00a616bd87075c8b1`.
- Single-slot serial profile `es3c28p-n16r8-m910-hugeapp-serial`: app 1,440,112 bytes / app0 3,145,728; app SHA `c45af61c2273996d856f91e79e2ad32dc989d3f9abb90278d2aed27418bba512`; manifest `ota_capable=false`.
- Frozen artifacts and set manifest: `/home/dell/workspace/task-artifacts/po6391-diagnosis-20261128/firmware-canonical-5.6.14/`; release-manifest SHA `87f6e40361920723be0c2daf7424daf22475e6b51e439fa6e393f753c828982f`. Built-in GTS Root R4 used; no device credentials/secrets embedded.

## Remaining gates

- PR #9 checks pass but GitHub review decision remains blank. Keep draft; do not bypass protection or merge without the required review gate.
- Release owner/PO6391 agent owns incident correlation and hardware inventory. Require exact physical UUID/profile, idle/no-queue/no-session, valid auth/config preservation and rollback proof before any pilot. No test board was proven attached/idle, so no pilot package was flashed.
- No tag, publication, merge, OTA upload, pilot or production deploy has occurred. Next action: obtain required review; only then continue release owner process, with one identified test target and rollback gate.
