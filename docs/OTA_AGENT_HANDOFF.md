# ESP32 Kiosk OTA — Agent Handoff / Release Runbook

> Last evidence refresh: 2026-11-21, Asia/Ho_Chi_Minh (UTC+7). This is operational context, **not** a release approval or proof of current device health.
> Canonical code owners: [MESFlow](https://github.com/hungtranbkit/mesflow/pull/50), [OTA Agent](https://github.com/hungtranbkit/mesflow-agent/pull/1), [firmware](https://github.com/hungtranbkit/mesflow-esp32-kiosk/pull/2). Never copy secrets, tokens, production database URLs, SSH credentials, or unredacted device inventory into Git.

## Read this first: system boundaries

| Layer | Responsibility | Release order |
| --- | --- | --- |
| MESFlow | Stable server instance identity, approved device enrollment, profiles, inventory and ownership; DB migrations 0055/0056 | PR #50 first |
| MESFlow / Deploy Agent | Instance-scoped OTA releases, artifact/profile compatibility, targeted forced batches, leases and retry limits, boot-identity ACK; heartbeat alone must **not** mark SUCCESS | Agent PR #1 second |
| ESP32-S3 Kiosk | Local safe-point checks, firmware A/B update, boot self-test, boot ACK, rollback, USB maintenance, scanner/keypad | Firmware PR #2 third |
| Test hardware | M910 bench node with ESP32-S3, USB serial and network | Test **one explicitly identified** board |
| QA runner | HP Linux machine, separate containers/worktrees for browser and PostgreSQL test gates | No shared production DB |

Do not assume which server serves production: **VPS vs Kimex cutover is NOT VERIFIED**. Inspect the live MESFlow instance UUID, Agent URL and deployment topology before touching production or changing DNS/tunnels.

## What was demonstrated vs. not demonstrated

- **2026-11-17**: authorized earlier ESP32 bench board had a real *staging* OTA A→B, explicit boot ACK, controlled rollback and restoration. Hardware gates G01–G18 passed; G19 was only a report-path requirement and remained blocked. This is **historical** evidence for that board and A/B artifacts, not proof for another device, a later binary or fleet deployment.
- **2026-11-18**: another ESP32-S3 bench board was flashed with v5.6.6 **via USB**; binary verification passed. This is not OTA-over-network evidence. Read live ROM identity, flash state, NVS/otadata and safe-point on the current board before any update.
- **2026-11-20**: on the exact reviewed feature heads, OTA Agent scoped tests: **74 passed**; firmware host C++ suites: **35 + 15 + 36 + 27 + 19 = 132 checks passed**; firmware Python tools: **43 passed**. These do not replace end-to-end hardware gates.
- **2026-11-21**: HP Docker 29.7.2 and PostgreSQL test container worked. Agent PR #1 `tests/test_p4_vertical_slices.py` ran with real disposable PostgreSQL: **4 passed, 0 skipped** (16.97 seconds). Earlier 2 backup/restore tests on M910 were SKIPPED due to Docker access, **not** PASS.
- **2026-11-21**: HP Docker + Playwright **1.62.1** launched an actual WebKit browser/newPage and loaded a test page, exit 0. MESFlow PR #50 source enumerated **613 browser tests in 83 spec files**; **full suite NOT run**. Smoke test != browser gate PASS.

### Exact feature heads at latest check

- MESFlow #50: `0f74793631f59a20ef33f08959b240c6446a3e24` — OPEN/DRAFT, no merge.
- Agent #1: `8a59f11cff2a49af134f1d0d8ea8fbc05e173f30` — OPEN, no merge.
- Firmware #2: `2843558f96a030e7e31f3eefc3edc948d43fc46f` — OPEN, no merge.

Check the current heads again before running acceptance. Earlier readiness docs refer to older heads: do not credit earlier test results to changed binaries.

## Known environment constraints / proven workarounds

**M910 (hardware bench).** USB enumeration can succeed while the Terminal MCP process cannot open its tty. In the observed user namespace, only UID 1000 maps and the tty appeared owned by unmapped `nobody:nogroup`, mode `0660`. `test -r` and `test -w` returned false; noninteractive sudo was unavailable. A **device-identity-scoped udev rule and guarded host-install script** were prepared locally, but **NOT installed**. Do not claim serial fixed until verified *on the live host*. Never use `chmod 777`, grant unrestricted USB access, reboot/flash an unidentified device, or erase NVS.

**M910 Docker.** `unshare -Ur true` returned `Operation not permitted`; Docker socket access returned `permission denied`. Rootless namespace-based test retries here are wasteful without fixing host access. Use HP's isolated Docker runner instead.

**HP Linux (QA).** Docker daemon 29.7.2 with overlayfs and cached `mcr.microsoft.com/playwright:v1.56.1-noble` image. The image contains browser engines but *not necessarily the Node Playwright library*. MESFlow declares `@playwright/test@1.62.1`; install the **matching** library and WebKit browser into a disposable test container. A version-matched 1.62.1 WebKit smoke **PASS** has been observed. HP disk was **93% used (~16 GB free)**; check space and reuse caches before large browser runs. Do not delete live volumes or datasets to clear space.

**Test isolation.** Stage exact PR SHA in separate Git worktrees; run disposable PostgreSQL containers and explicit test config. Never point browser/backup tests at production. No CI result, old log, or `SKIPPED` status substitutes for an actual exit-0 PASS.

## Mandatory release gate / recovery procedure

1. Re-read `AGENTS.md`, this document and the **local private site settings** (if available). Confirm node, repositories, exact heads, runtime permissions, disk, USB identity and live deployment topology.
2. Run MESFlow database migrations 0055/0056, downgrade/restore and isolated Docker image/package/backup tests on exact PR head. Ensure full deterministic WebKit/browser suite passes or report every blocked case.
3. Confirm external deployment hooks (webhooks, GitHub App, downstream and host-side auto deploy) cannot unexpectedly deploy from a merge; read current VPS/Kimex target before changes.
4. On **one approved bench board**: verify live ROM MAC against an allowlist, verify current full flash backup SHA/checksum, app image and partition SHA/profile, enrollment scope, NVS preservation, safe point (queue empty, no pending business operations), network/TLS/Agent and rollback image availability.
5. Only with all safety prerequisites PASS, execute one scoped **network OTA A→B**, verify job state transitions, explicit boot-identity ACK, new running firmware, success and a controlled rollback/recovery drill. USB flashing is a separate action and cannot count as OTA.
6. Only after all release gates PASS on current SHAs, consider merging in this order: MESFlow #50 → Agent #1 → firmware #2. Canary release to a single authorized device first. **No mass OTA** from this runbook.
7. Preserve pre-deploy backups and rollback instructions. **Old MESFlow main must not start against schema 0056** without using the new migration code to downgrade to the recorded compatible schema first; downgrade discards new enrollment/profile metadata.

## Agent task execution contract

- Receipt `SUBMIT_CONFIRMED` means prompt was delivered, **not** that Codex is executing. Check whether the prompt is still in the composer; require runtime logs, file changes, command exit statuses and time-stamped evidence before calling it RUNNING or DONE.
- Prefer durable, restart-safe, noninteractive tasks with idempotency keys; avoid queueing duplicate long prompts in a stale interactive shell. Do not send blind Enter to an unknown terminal/session.
- For each checkpoint log **đã làm / đang làm / sẽ làm**, PASS/FAIL/BLOCKED/SKIPPED per gate, Git SHA, USB identity source, timestamps, hard blockers and top three next actions. Never mark a skipped test PASS.
- Scope of change: testing and bench only until release gate is proven; no live DB/DNS/tunnel/production changes merely for convenience. Require a positive audit trail for canary OTA and rollback.

## Agent on-boarding / discoverability

Read `AGENTS.md` at the repository root. On a connected local node, read its private `OTA_SITE_SETTINGS.local.md` (maintained outside Git) for absolute paths, device IDs and current executable commands. This public runbook deliberately omits internal addresses and hardware serial IDs. If the private file is absent, discover and verify local settings afresh rather than guessing.

Update the **date** and **exact commit SHA** each time an acceptance gate changes. Keep historical evidence as history; do not overwrite it with a current-success claim.
