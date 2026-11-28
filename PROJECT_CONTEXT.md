# ESP Kiosk project context

## Canonical source / integration state

- `main` at remote `92a80d4ce6ce3c5f169a7d7798ed2309c856b085` is still the legacy 5.5.7 source line. The unified firmware candidate is on `integration/esp-canonical-release-20261128`, PR #9 → `main`; PR remains draft until exact-head CI and source review gates pass.
- Firmware code commit `bf1cf9cbb18b2f0333ad40c09bf171b46fb0c130` advances to 5.6.15 / build `20261128.0005`. It includes the 5.6.11 lineage, UTF-8/OPID, numeric ID/approval, Wi-Fi cancel/multi-SSID, OTA/deep-sleep safeguards, credential write/readback rollback, stable boot ACK application semantics, legacy OTA response compatibility, no POST-body logging, and idle READY connectivity redraw.
- `docs/ESP_BRANCH_INTEGRATION_MATRIX_20261128.md` is the fetched 24-ref branch reconciliation. One canonical source branch is maintained; remaining USB auto-provision delta from PR #2 is held because companion MESFlow PR #52 is still open/unmerged. Do not merge PR #2 or expose that protocol until the server contract is reviewed.
- Common application source serves all kiosk profiles. Verified hardware facts belong in `profiles/*.json`; unknown boards stay inactive/UNVERIFIED. JC4827W543 is a separate NV3041A QSPI test display and is not compatible with the current ES3C28P ILI9341 production image.

## Verification and artifacts

- At code SHA `bf1cf9c`: 10 C++ host suites PASS, 87 Python regression/release tests PASS, and `git diff --check` PASS. CI workflow now includes the keypad diagnostic source contract and scanner UTF-8/OPID C++ suite.
- Pinned Dell build PASS for verified dual-slot OTA profile: app 1,448,064 / 3,342,336 bytes; app SHA `5ebe988f07dcd739b6f577c85e934b9a09555b3da07c3d920b716d7959146b8b`; OTA ZIP `2cfcc0a28ef82b540f9fe0ed31ee8ea4e72b0107079b12c622372724c6fb9be7`; release verifier PASS.
- Pinned Dell build PASS for verified single-slot huge_app: app 1,448,064 / 3,145,728 bytes; app SHA `1a0318533c841987de7865231aa77e9276e2cb4b73df96456c9e7194d519494a`; app-only manifest says `ota_capable=false`.
- Local candidate files/manifests/checksums: `/home/dell/workspace/task-artifacts/po6391-diagnosis-20261128/firmware-canonical-5.6.15-bf1cf9c/`. These are not production-promotion artifacts. Use exact merged-main CI artifacts after merge.
- Current post-push PR #9 exact-head CI is pending; do not merge until pass. `main` protection/rulesets were absent at last read, but do not bypass any checks/reviews if policy changes.

## Verified profile and hardware gates

- `es3c28p-n16r8-m910-gm65uart1`: 16 MiB flash / 8 MiB OPI PSRAM / ILI9341 240x320 / GM65 RX GPIO44, default_8MB dual slots 3,342,336 bytes; partition hash `1d9cca96de0fe07ad7fc0648b9878ddecd9ce565e38b589ad20fea698ed4c80c`.
- `es3c28p-n16r8-m910-hugeapp-serial`: verified single app0, 3 MiB at `0x10000`; partition hash `aaae2888c5a6a348004b5b436f47abb25ae32e72d9003902955a998eda723edd`; no OTA slot.
- Kimex Máy 1 UUID `fc0c33a9-2f76-424f-a256-217699b57712` lacks MAC/board/partition join. Máy 2/A UUID `0f2d2ad3-06d7-417f-a1e7-da3d2457db39`, MAC `84:C7:BB:79:39:84` is the single-slot receipt; no OTA. Kiosk Linux USB MAC `28:84:85:85:75:10` remains `UNVERIFIED`. JC4827W543 is test-only.
- No production device has verified UUID + exact profile + idle/no-queue/no-session + current backup and rollback. No device has been reset, flashed, sent OTA, or had NVS/config/auth changed in this task. This is the concrete pilot blocker.

## Recent handoff

- 2026-11-28: Reconciled remote branch tips; ported missing OTA ACK/logging and READY redraw fixes; preserved auth safely; added profile/branch matrices and local candidate builds. PR #9 is the only source integration path. Next: wait for exact-head checks, mark ready and merge only if gates pass; then use post-merge main CI artifact. Keep production flash blocked until an exact idle rollback-capable target is identified.
