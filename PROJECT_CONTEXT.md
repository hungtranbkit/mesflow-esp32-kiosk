# ESP Kiosk project context

## Canonical source and branch policy

- Canonical firmware source is `main` at PR #10 merge commit `b4c85c9837a3c0178ee3bca7ccf97ccd22e7c8ad`, firmware 5.6.16 / build `20261128.0006`. It unifies the 5.6.11 lineage and the numeric-ID, approval, Wi-Fi cancel/multi-SSID, OTA/deep-sleep/rollback, credential-preservation, scanner UTF-8/OPID, operator worker-guard, exact duplicate suppression and gated scanner-probe behavior.
- All hardware uses the shared app source. Separate build/profile records are allowed only for proven display, pin, flash/PSRAM, partition, scanner wiring/baud and OTA capability. Unknown hardware stays inactive/UNVERIFIED. Each coding task owns one branch/worktree; hardware-specific long-lived source branches are reconciliation inputs, not product lines.
- PR #9 reconciled the initial 24-ref inventory; a direct `git ls-remote --heads origin` now reports 25 refs including the retained follow-up branch. PR #1 had two source gaps missed by the first comparison: exact repeat suppression for 3 seconds and `scanner-probe`; PR #10 ported and merged them. PR #1 and PRs #5–#8 are closed as superseded; source branches are retained, not deleted. PR #2 remains draft because companion MESFlow PR #52 is open/unmerged.

## Merged 5.6.16 scanner changes

- `esp/mesflow_app.cpp`: adds maintenance-only `scanner-probe`, restricted to idle/no-session/no-queue/no-pending-command/no-OTA state and two verified ES3C28P M910 profile IDs. It samples the configured GPIO44 RX pin and drains buffered scanner bytes without changing baud, NVS, Wi-Fi, identity, auth or OTA state. Unknown/other profiles are rejected.
- `esp/mesflow_scanner_qr_policy.h`: exact repeat suppression policy for syntactically valid `WF|EMP|`, `WF|OP|` and `WF|OPID|` frames within 3000 ms. Duplicate frames are dropped before payload output and dispatch; per-byte scanner logs no longer echo payload bytes.
- `esp/mesflow_maintenance_policy.h`, `tests/host/test_*`, `tools/test_scanner_safety_contract.py`, `.github/workflows/esp-ota-ci.yml`: pure policy tests and USB/profile/source contracts are included in CI.
- Version is 5.6.16 / build `20261128.0006`, merged PR #10 at `b4c85c9`; PR head `53214564eefe380517bb4aae4c654464888b1347` passed exact-head CI. Source merge is separate from device qualification. Do not flash: no exact production device has current UUID + board + partition + idle + backup + rollback proof.

## Hardware profile facts and release boundaries

- See `docs/FIRMWARE_PROFILE_MATRIX_5_6_15.md` for device evidence and `docs/FIRMWARE_PROFILE_MATRIX_5_6_16.md` for the scanner candidate. Verified M910 profiles are dual-slot `es3c28p-n16r8-m910-gm65uart1` and single-slot serial-only `es3c28p-n16r8-m910-hugeapp-serial`; these binaries are not interchangeable.
- Kimex Máy 1 UUID `fc0c33a9-2f76-424f-a256-217699b57712` has no MAC/board/partition join. Máy 2/A UUID `0f2d2ad3-06d7-417f-a1e7-da3d2457db39`, MAC `84:C7:BB:79:39:84`, is verified single-slot huge_app and cannot OTA. Kiosk Linux MAC `28:84:85:85:75:10` remains UNVERIFIED. JC4827W543 is a separate test-only QSPI display and cannot use the ES3C28P ILI9341 image.
- No device reset/flash/OTA/NVS/config/auth mutation was performed. Do not infer target identity from USB descriptors or model names.
- USB provisioning from PR #2 remains held on backend PR #52. Keep PR #1 open until the scanner follow-up merges; keep PR #5–#8 as recovery/history until their meaningful behavior is confirmed on main. Do not force-push or delete unreconciled refs.

## Verification and artifacts

- At code SHA `bf8b0e011e95c26005c1e542e45a2cf1f1bab856`, local official host gate: 10 C++ host suites PASS; official Python set PASS (91 tests, including new scanner safety tests); `git diff --check` PASS. Pinned dual-slot M910 OTA build PASS at 1,449,120/3,342,336 bytes, app SHA `328fe64877864fe93894c1a9e225a87e6a89e54a71a8b6063c7c6b63bb97c5c5`; package SHA `1a7e7b1b4f1fe4683bc650e872a41a51179f4ec77073e45f2704b8a14a79f959`; verifier PASS. Single-slot serial build PASS at 1,449,136/3,145,728 bytes, SHA `07fa153cea5bac778ce24cd83e9fe46d455ce3692ddefe91391123666b7ea89d`, no OTA capability. Kiosk Linux unverified profile skipped. Exact PR #10 head CI run `46153629479`: host-tests and build-release PASS; merge commit `b4c85c9`. Main post-merge CI run `46154080329`: host-tests PASS; build-release pending at last status check.
- Artifact bundle and `SHA256SUMS`: `/home/dell/workspace/task-artifacts/po6391-diagnosis-20261128/firmware-canonical-5.6.16-bf8b0e0/`; checkpoint: `/home/dell/workspace/task-artifacts/po6391-diagnosis-20261128/firmware-canonical-release-handoff.md`. 5.6.15 artifacts remain preserved under `firmware-canonical-5.6.15-bf1cf9c/`.

## Recent handoff

- 2026-11-28: PR #9 merged 5.6.15; PR #10 merged 5.6.16 after exact-head CI success. PR #1 and #5–#8 closed as superseded with source branches retained. Follow up on post-merge main CI and release artifact promotion. No hardware writes until a unique safe rollback-capable target is proven.

## USB provisioning candidate handoff (2026-11-28)

- Main remains canonical 5.6.16 / `20261128.0006` at PR #11 merge `0c1d1adc`; this task branch `agent/codex/esp-usb-provisioning-5616` is based on that SHA and is the only firmware integration candidate. PR #2 / 5.6.6 is a source reference only; do not use its old binary or merge its old base.
- Firmware USB protocol is being ported as candidate 5.6.17 / `20261128.0007`. It runs only through an explicit USB serial command and maintenance gate; no auto watcher/service is installed. Server-side provisioner stays disabled unless the dedicated hashed service token and exact UUID allowlist are configured. Do not flash pending hardware acceptance.
- `esp/mesflow_app.cpp` adds read-only hello, challenge-bound HMAC proof, exact derived-ID commit, and config application guarded by UUID+MAC, unbound/no-credential/idle/no-queue/no-OTA state. Config writes are limited to primary SSID/password, server, station, and derived device ID; existing non-default differing values fail closed, with readback/restore on failure. UUID/token, secondary SSIDs, OTA agent and display name are not written. Secret-bearing USB command lines are redacted in serial logs.
- `esp/mesflow_maintenance_policy.h` and host tests constrain protocol commands to maintenance; CI runs the USB policy and source preservation contracts. Candidate local verification: 10 C++ host suites PASS; Python official set was rerun after version update (record final result below); app build pending. JC4827W543 stays test-only/UNVERIFIED; no new profile or pin assumptions.
- MESFlow companion branch is separately based on current `main` 8c595689 and changes legacy PR #52 migration 0057 collision to additive migration 0058 after deployed .396's 0057 display-number head. Strict PostgreSQL tests and isolated migration upgrade were run; full official Docker E2E gate still active at checkpoint time. No production migration/deploy or device write was performed.
