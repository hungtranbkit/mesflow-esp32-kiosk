# esp-kiosk

Read and obey the workspace rules first:

`../AGENTS.md`

Project directory:
`esp-kiosk/`

Do not modify sibling projects unless the task explicitly requires a cross-project change.

## Firmware source and hardware profile integration

- Keep one canonical firmware integration/release branch at a time; do not maintain long-lived release variants for individual kiosk models.
- All supported devices share the canonical application source. Use profile/build flags only for hardware facts that are backed by an accepted board receipt or verified partition/build evidence (display/controller, pin map, flash/PSRAM, partition table, scanner wiring/baud and OTA capability).
- Each coding task owns one agent branch and dedicated worktree. Device-specific firmware branches are reconciliation inputs, not long-lived product lines; merge valid behavior into the shared source and keep only verified profile/build differences.
- A new or uncertain board stays `UNVERIFIED` and excluded from release artifacts until its profile is proven. Never infer identity from USB product strings or model names, and never use an image built for a different partition/display profile.
- Before retiring a branch, compare its unique behavior and tests against canonical source, record the decision in `docs/ESP_BRANCH_INTEGRATION_MATRIX_20261128.md`, preserve any unresolved unique work, and keep review/CI gates. Never force-push or delete unreconciled history.
- Source merge qualification is separate from OTA/serial pilot qualification. No production write without exact device identity, matching profile, idle/safe-point evidence, credential/config preservation and a demonstrated rollback route.
