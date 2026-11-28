# ESP Kiosk project context

## Canonical source and branch policy

- Canonical firmware source is `main` at PR #9 merge commit `4d10dbc54763abfe312531834d904b31e7983cdd`, firmware 5.6.15 / build `20261128.0005`. It unifies the 5.6.11 lineage and the numeric-ID, approval, Wi-Fi cancel/multi-SSID, OTA/deep-sleep/rollback, credential-preservation, scanner UTF-8/OPID and operator worker-guard behavior.
- All hardware uses the shared app source. Separate build/profile records are allowed only for proven display, pin, flash/PSRAM, partition, scanner wiring/baud and OTA capability. Unknown hardware stays inactive/UNVERIFIED. Each coding task owns one branch/worktree; hardware-specific long-lived source branches are reconciliation inputs, not product lines.
- PR #9 reconciled the fetched 24-ref inventory. The PR #1 legacy branch had two actual source gaps missed by the first comparison: exact repeat suppression for 3 seconds and `scanner-probe`. Follow-up `agent/codex/esp-scanner-safety-5616` is based on merged main and carries those changes as 5.6.16; it is a narrow feature branch and the only active follow-up to canonical source.

## Current 5.6.16 scanner follow-up

- `esp/mesflow_app.cpp`: adds maintenance-only `scanner-probe`, restricted to idle/no-session/no-queue/no-pending-command/no-OTA state and two verified ES3C28P M910 profile IDs. It samples the configured GPIO44 RX pin and drains buffered scanner bytes without changing baud, NVS, Wi-Fi, identity, auth or OTA state. Unknown/other profiles are rejected.
- `esp/mesflow_scanner_qr_policy.h`: exact repeat suppression policy for syntactically valid `WF|EMP|`, `WF|OP|` and `WF|OPID|` frames within 3000 ms. Duplicate frames are dropped before payload output and dispatch; per-byte scanner logs no longer echo payload bytes.
- `esp/mesflow_maintenance_policy.h`, `tests/host/test_*`, `tools/test_scanner_safety_contract.py`, `.github/workflows/esp-ota-ci.yml`: pure policy tests and USB/profile/source contracts are included in CI.
- Candidate version is 5.6.16 / build `20261128.0006`. Source qualification is separate from device qualification. Do not flash: no exact production device has current UUID + board + partition + idle + backup + rollback proof.

## Hardware profile facts and release boundaries

- See `docs/FIRMWARE_PROFILE_MATRIX_5_6_15.md` for device evidence and `docs/FIRMWARE_PROFILE_MATRIX_5_6_16.md` for the scanner candidate. Verified M910 profiles are dual-slot `es3c28p-n16r8-m910-gm65uart1` and single-slot serial-only `es3c28p-n16r8-m910-hugeapp-serial`; these binaries are not interchangeable.
- Kimex Máy 1 UUID `fc0c33a9-2f76-424f-a256-217699b57712` has no MAC/board/partition join. Máy 2/A UUID `0f2d2ad3-06d7-417f-a1e7-da3d2457db39`, MAC `84:C7:BB:79:39:84`, is verified single-slot huge_app and cannot OTA. Kiosk Linux MAC `28:84:85:85:75:10` remains UNVERIFIED. JC4827W543 is a separate test-only QSPI display and cannot use the ES3C28P ILI9341 image.
- No device reset/flash/OTA/NVS/config/auth mutation was performed. Do not infer target identity from USB descriptors or model names.
- USB provisioning from PR #2 remains held on backend PR #52. Keep PR #1 open until the scanner follow-up merges; keep PR #5–#8 as recovery/history until their meaningful behavior is confirmed on main. Do not force-push or delete unreconciled refs.

## Verification and artifacts

- At follow-up source SHA (see branch/PR head), local official host gate: 10 C++ host suites PASS; official Python set PASS (91 tests, including new scanner safety tests); `git diff --check` PASS. Pinned profile builds and final CI status are tracked in PR #10 and the release handoff.
- Current profile matrix and build evidence are recorded under `docs/`. Artifact files, checksums and release handoff are stored under `/home/dell/workspace/task-artifacts/po6391-diagnosis-20261128/firmware-canonical-release-handoff.md`; 5.6.15 artifacts remain preserved under `firmware-canonical-5.6.15-bf1cf9c/`.

## Recent handoff

- 2026-11-28: PR #9 merged 5.6.15. Semantic re-review found two missing PR #1 scanner changes; ported them safely as the 5.6.16 follow-up branch above. Next: finish local profile builds/artifact checks, push/open PR #10, wait for exact-head required CI/review, merge only on gate pass, update branch matrix and handoff. No hardware writes until a unique safe rollback-capable target is proven.
