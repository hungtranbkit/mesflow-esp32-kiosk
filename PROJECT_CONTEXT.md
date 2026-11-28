# ESP Kiosk project context

This is the current handoff for the firmware repository. Source, tests, git state, and device evidence take precedence over older notes.

## Current canonical release candidate

- Branch: `integration/esp-canonical-release-20261128`, dedicated worktree `/home/dell/workspace/mesflow/.worktrees/integration-esp-canonical-release-20261128`.
- Branch starts at `origin/main` `92a80d4` (legacy 5.5.7), merges the complete 5.6.11 candidate lineage from PR #8, and therefore carries the path needed for a direct PR to `main`. It includes PR #4's accepted numeric kiosk ID, approval/identity and OTA/deep-sleep baseline; scanner UTF-8/`WF|OPID|`; Wi-Fi setup cancel; and the outstanding multi-SSID behavior ported from PR #1's `fe46523`.
- Source release commit: `6492beb89ae9e1fe96cd67a511c9acb82fa57da5`, firmware `5.6.13`, build prefix `20261128.0003`; candidate is not merged, tagged, published or installed. The earlier 5.6.11 candidate and unpinned 5.6.12 local artifact remain superseded; see `docs/FIRMWARE_PROFILE_MATRIX_5_6_13.md`.
- Main remains unchanged. PR #6/#7/#8 are older stacked drafts and are not the canonical release path; do not merge them independently ahead of the integrated PR.

## Firmware contracts to preserve

- Scanner frame policy in `esp/mesflow_scanner_qr_policy.h` keeps UTF-8 bytes, strips control bytes, and retains ASCII-ratio baud recovery. The `WORKER_REQUIRED` guard still returns before operation lookup. `WF|OP|` and `WF|OPID|` route into existing operation handling.
- Wi-Fi cancellation tears down the setup AP and resumes the primary SSID without saving or clearing NVS. Multi-SSID state is stored only as `wifi_ssid2/pass2` and `wifi_ssid3/pass3` in `mesflow_cfg`; config writes validate lengths and read back fallback values. The code does not rewrite identity UUID/secret, approval token, server, or station namespaces during Wi-Fi switching.
- Numeric display ID, server-scoped credentials/approval, USB-only maintenance, GM65 UART1, keypad/PCF8574, deep sleep and OTA/sleep/setup interlock, inactive-slot image/hash validation, boot confirmation and rollback remain on the integrated baseline.
- See `docs/NUMERIC_DISPLAY_ID.md`, `docs/M910_HUGE_APP_SERIAL.md`, and `docs/FIRMWARE_PROFILE_MATRIX_5_6_13.md` for detail.

## Device/profile evidence and limits

- Kimex Máy 1 UUID `fc0c33a9-2f76-424f-a256-217699b57712` was reported as ES3C28P 5.6.6. Its exact MAC, LCD wiring, partition table and OTA slot count are not tied to the UUID, so no device-specific image is assigned.
- Kimex Máy 2 / A UUID `0f2d2ad3-06d7-417f-a1e7-da3d2457db39`, MAC `84:C7:BB:79:39:84`, has verified `huge_app` one-slot table SHA `aaae2888c5a6a348004b5b436f47abb25ae32e72d9003902955a998eda723edd`, 3 MiB app0 at `0x10000`; it is serial-app-only and not OTA-capable. This does not identify the PO6391 screenshot.
- Historical M910 UUID `2eebb378-e0dd-4c3b-aa4d-39ae012437e0` was last seen 2026-11-11 at firmware 5.6.2. Repo has separate VERIFIED M910 `default_8MB` dual-slot and `huge_app` single-slot build profiles, but that stale identity has no fresh partition proof. Do not target it from profile name alone.
- Dell kiosk1 USB reference MAC `28:84:85:85:75:10` remains unverified (`kiosklinux` profile); serial was not opened. No production device was flashed or OTA-updated.
- PO6391 hardware attribution and current idle/rollback state remain unproven. The 2026-11-28 checkpoint separates local `WORKER_REQUIRED` from scanner payload behavior and heartbeat timeout; no new root-cause evidence has been added in this release branch. See `/home/dell/workspace/task-artifacts/po6391-diagnosis-20261128/checkpoint.md`.

## Verification and artifacts

- Final source SHA local host gate: 9 C++ suites with `-Wall -Wextra -Werror` PASS (includes 1,000 threaded race interleavings); official Python tool list 65/65 PASS.
- Local build toolchain: ESP32 core 3.3.11, GFX 1.13.0, ILI9341 1.7.0, BusIO 1.17.4, ArduinoJson 6.21.5, isolated under `/tmp/esp-canonical-release-arduino-user`. Release ZIP verifier PASS.
- OTA profile artifact SHA `7bd88fcded7db31538ae532114c0052e9684ed8cf115f7edac55cc9300fb8df6`, 1,439,152 bytes in 3,342,336-byte slots; archive SHA `363dad1742b4cb2c6d878463f73247567acbd0e5cddd52f3f82e88661f07f294`.
- Serial huge_app app SHA `3bd21bf3455f7f770db01ae192f64857f034d50ee275b72f7af2bc693ff2c25e`, 1,439,152 bytes in 3,145,728-byte app0; manifest declares `serial-app-only` and `ota_capable=false`. Unverified Kiosk Linux profile was skipped.
- Frozen binaries and manifests are outside git at `/home/dell/workspace/task-artifacts/po6391-diagnosis-20261128/firmware-canonical-5.6.13/`. Superseded local 5.6.12 unpinned build is under `retired-v5612-unpinned/`; it is not a release input.

## Release gates / next actions

1. Push the canonical integration branch and open a draft PR to `main`; wait for required CI and review. Do not bypass branch protection or merge a failing/pending gate.
2. Update this context and the separate `firmware-canonical-release-handoff.md` with the resulting PR/check status and final head SHA.
3. Do not create or upload an OTA job until a target UUID is joined to a fresh actual partition/profile and auth enrollment. Do not flash/OTA production in this task: no verified idle test M910 and rollback-capable physical target is currently available. The serial-only A/Máy 2 artifact is not a pilot substitute.
4. The PO6391 diagnosis agent owns further scanner/worker/HB and inventory investigation; coordinate by reading its checkpoint, without changing its worktree or writing device state.

No database/schema change. No production write, OTA upload, serial access, reset, flash, NVS erase, merge, tag, or deployment occurred.
