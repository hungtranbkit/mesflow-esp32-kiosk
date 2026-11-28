# ESP32 branch integration matrix — 2026-11-28

Inventory was refreshed with `git ls-remote --heads origin` and `git fetch origin +refs/heads/*:refs/remotes/origin/*`. This matrix covers the 22 non-`main`/non-canonical branch refs plus the separately listed `main` and canonical branch (24 refs total). Hashes below are the exact fetched remote tips at inventory time; confirm again before merge/cleanup. Source status and test references reflect the integration candidate, not a hardware deployment.

| Ref at fetched tip | Exact SHA | Unique change / comparison | Disposition |
|---|---|---|---|
| `adapt/v5.6.0-m910-gm65-20261110` | `ea192e2ce85318c168949611b602e469a3f244b5` | Ancestor; M910 source adaptation and 115200 scanner/runtime handling are already in canonical lineage. | Integrated; no merge needed. |
| `agent/claude/v1-dev-reconnect` | `fe46523b76cfa91f4e2beacc8981f7945797d650` | Legacy multi-OP/session, reconnect, multi-SSID, scanner frame/auto-baud and worker/action-queue behavior is present in canonical source and lineage tests. | Integrated by later commits/refactors; test current canonical source. |
| `agent/codex/esp-approval-identity-v567-20261127` | `813df3e065651584d1f9c3263e6591cabd1b8c32` | Ancestor; approved identity/display/approval behavior is present. | Integrated; no merge needed. |
| `agent/codex/esp-display-identity-20261127` | `ecd93e0f092cff13c9dc5dad8ea5fcc9e41040d6` | Keypad diagnostics/performance counters, software scanner window, scheduled deep sleep, OTA/sleep gate. Current source includes these APIs/policies; keypad source-contract test is now included in CI. Hardware sleep/window remains guarded and documented as unqualified for production. | Integrated equivalent; keep board behavior behind existing guards; verify CI on final head. |
| `agent/codex/esp-display-number-v5610-20261127` | `9ad8c963cddceb1ea58271aeb6d6aea5154ba817` | Ancestor; display-number/identity behavior is included. | Integrated; no merge needed. |
| `agent/codex/esp-po6391-v5611-integration-20261128` | `d6dd2610b9165ab42b1d7ecb29c24b7511c55cfc` | Ancestor; scanner/Wi-Fi cancel, sleep/deep-sleep and lineage tests are included. | Integrated; no merge needed. |
| `agent/codex/po6391-qr-utf8-fix` | `81f755a802fea248e97db07c1864aa9e6b529b4a` | UTF-8 data-byte handling and OPID scanner policy are present in canonical source with native/source regression tests. | Integrated equivalent in canonical PR #9; no cherry-pick. |
| `audit/ota-contract-20261113` | `b5203cd116e0a73c984f9cff28cb1ba9c8e13b3c` | Unique: stable OTA boot-ACK client_event_id, honor response.applied for v2 while accepting legacy responses without applied; remove raw POST-body logging. | Ported in this candidate with C++ policy + Python source regression tests; requires new exact-head CI. |
| `backup/esp-kiosk-v5.5.7-20261110` | `92a80d4ce6ce3c5f169a7d7798ed2309c856b085` | Exact same SHA/tree as main 92a80d4. | Backup only; no delta. |
| `docs/gm65-setup-reuse-20261123` | `058c4fdcdb58424814dc48a2dddfa3a9bc12c9cc` | Manual-derived GM65 setup values plus strict statement that no physical configuration/scan was observed. Also carries USB-provision code from PR #2. | Three setup reference docs imported; USB code is deferred with PR #2 pending backend PR #52. |
| `docs/ota-agent-handoff-20261121` | `64573e7c69c6d752fddf470357f528f21da4a6a4` | Dated release/QA handoff with device and staging evidence; not current source behavior. | Historical only; do not make stale NO-GO/checkpoint text current policy. Keep ref/evidence, no source merge. |
| `feat/esp-deep-sleep-20261124` | `21bc3536afec281de82a524011f19f0c7a966aae` | Ancestor; timer/deep-sleep implementation and safe guards are in canonical source. | Integrated; no merge needed. |
| `feat/esp-ota-force-scoped-20261111` | `2843558f96a030e7e31f3eefc3edc948d43fc46f` | Canonical includes scoped OTA/safe-point/rollback. Unique USB enrollment/provision protocol and host policy require companion MESFlow PR #52 (`fix/kiosk-trusted-enrollment-issue51`) which GitHub reports OPEN/unmerged (head f11e0388). | Hold only USB-provision delta; do not merge/use until backend contract, security review and exact device enrollment gate pass. PR #2 must remain draft/blocked. |
| `feat/gm65-rest-window-20261123` | `14d2cbcf1fc7e35f34a3ab5928256b054a7de764` | Ancestor; software-only scanner input window is present and documented not to represent physical scanner sleep. | Integrated; no merge needed. |
| `fix/deepsleep-ota-gate-20261124` | `c0a80ce3d3c8888ffd07ddce5e175eed2636eeb4` | Ancestor at c0a80ce3; numeric ID, approval, OTA partition/rollback and deep-sleep interlock are in canonical lineage. | Integrated; no merge needed. |
| `fix/esp-kiosk-hardware-gm65-20261110` | `99b932263afe234be99f5f187afb830db3b2c9bd` | Optional MESFLOW_GM65_BAUD=9600/115200 compile switch and explicit old-vs-module baud caution are present; current M910 profile evidence does not justify setting a new per-device default. | Source equivalent; matrix records baud uncertainty. Do not create a production 9600 profile without board/module evidence. |
| `fix/esp-kiosk-link-repaint-20261110` | `8262e7b4f2ef283d4cb6f1f9f2285008eb04925f` | Unique idle READY redraw on online/offline transition; current candidate lacked this. | Ported in this candidate with source contract test. |
| `fix/esp-kiosk-scanner-diagnostics-20261110` | `ff66983b362b3a5f45a8d6201dd805676f3bb8b7` | Scanner aggregate counters/status and GTS Root R4/HTTPS hardening are present in canonical source. | Integrated equivalent; no merge needed. |
| `fix/esp-kiosk-station-bind-persist-20261110` | `a502bba855efef6a5d7b890a6032ddaec5b1bfdb` | Server-approved station is persisted and old station/token namespaces are reconciled on boot. | Integrated equivalent; no merge needed. |
| `fix/kiosk-qr-confirmation-hold-v562-20261111` | `24100836fee7a8227df8ee30632a1cbebaecebc0` | Ancestor; QR confirmation hold is in canonical lineage. | Integrated; no merge needed. |
| `fix/scanner-utf8-v5610-20261128` | `ad37cba01cdce9797fa6aa54b87217eb9b36172b` | Ancestor; scanner UTF-8 source and tests are in canonical lineage. | Integrated; no merge needed. |
| `fix/wifi-portal-cancel-v5610-20261128` | `48bd23c9613ddd85f8959ee840b40b5f88ce5e13` | Cancel behavior is patch-equivalent in canonical source; portal lifecycle and non-destructive cancellation tests are included. Branch adds a dated evidence report. | Imported dated evidence doc; code not duplicated. |

| `main` | `92a80d4ce6ce3c5f169a7d7798ed2309c856b085` | Legacy source baseline (5.5.7 line); no hardware profile inference. | Merge target only through the protected/reviewed source PR. |
| `integration/esp-canonical-release-20261128` | `5743c0bcacaa634845c73f7a675dc7470f61ce8e` | Canonical release candidate, now receiving only valid missing deltas and integration fixes. | PR #9 → `main`; one canonical owner/branch. |

## Hardware distinction that remains unresolved

`JC4827W543_TEST_PROFILE.md` records a separate test board: ESP32-S3-WROOM-1-N4R8, 4 MiB flash, 8 MiB OPI PSRAM, NV3041A 480×272 QSPI (CS45/SCK47/D0=21/D1=48/D2=40/D3=39, backlight GPIO1), MAC `a4:cb:8f:ec:1d:9c`. It is not in the current profile build set, and the production kiosk app uses the ES3C28P ILI9341 SPI driver. Keep the board test-only and inactive until a distinct tested display/partition profile and release manifest exist. USB product strings do not prove a board model.

## Merge and cleanup policy

- Source merges are qualified by exact-head CI and review; OTA/serial pilot qualification is a separate gate. Do not bypass protection or merge an old PR head.
- Keep PR #2/USB provisioning blocked by the unmerged MESFlow PR #52 dependency; preserve branch/history and do not expose the serial provisioning feature as releasable.
- Do not delete refs until every unique change is in canonical or explicitly archived with the reason above. No force-push was used.
