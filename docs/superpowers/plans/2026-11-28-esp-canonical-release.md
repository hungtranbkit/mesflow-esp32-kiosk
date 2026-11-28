# ESP32 Canonical Release 5.6.13 Implementation Plan

> **For agentic workers:** execution uses the existing owner session and dedicated integration worktree.

**Goal:** Combine the newest verified kiosk baseline with scanner, Wi-Fi cancel, and multi-SSID changes into one branch based on current `main`.

**Architecture:** Merge the 5.6.11 candidate lineage into an isolated branch from `origin/main`, port the outstanding multi-SSID behavior, then build only profiles marked VERIFIED. Device rows without partition evidence remain unbuildable/unflashable target profiles.

**Tech Stack:** ESP32-S3 Arduino CLI 3.3.11, C++ host policy tests, Python source-contract/release tests, GitHub PR checks.

**Spec:** User release-owner instructions in the task request; repo rules in `AGENTS.md` and `esp-kiosk/AGENTS.md`.

## Global Constraints

- Preserve UUID/token/server/station and NVS namespaces; do not reset, erase, or flash without exact target and rollback evidence.
- Do not use the serial-only huge_app binary as an OTA package or as an assumed Kimex profile.
- Do not promote an unverified partition profile.
- Release version must advance past the already frozen 5.6.11 candidate.
- No production write in this task; only a single verified idle test pilot can be considered after release gates.

## Review Focus

- Scanner UTF-8 and `WF|OPID|`, with `WORKER_REQUIRED` guard before lookup.
- Setup cancellation restores the configured active Wi-Fi without saving/clearing NVS.
- Multi-SSID load/save/failover preserves auth and server settings.
- OTA/deep-sleep interlock and rollback remain unchanged.
- Profile table hash, app capacity, version/build identity, and manifest checksums match each artifact.

### Tasks

- [x] Merge candidate lineage to a branch rooted at current `origin/main`.
- [x] Port three-network failover and host/source-contract regressions.
- [x] Run host tests and per-verified-profile release builds using pinned local libraries; record exact artifacts.
- [x] Review source diff, update context and release handoff.
- [x] Push branch and open canonical draft PR; keep draft until required CI/review pass.
- [x] Keep physical pilot blocked until the PO6391 device is uniquely identified, idle, and rollback-capable.
