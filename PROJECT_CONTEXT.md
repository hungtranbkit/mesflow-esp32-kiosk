# ESP Kiosk project context

## PO6391 QR regression handoff (2026-11-28)

- Repository/worktree: `esp-kiosk`, branch `agent/codex/po6391-qr-utf8-fix`, based on source `92a80d4`; repo source identifies firmware 5.5.7. Production device build/version is not verified.
- Scanner decoded QR bytes were filtered to printable ASCII in `esp/mesflow_app.cpp::dispatchScannerFrame`, stripping Vietnamese UTF-8 bytes before operation lookup. `esp/scanner_qr.h` now keeps UTF-8 data bytes and recognizes both `WF|OP|` and `WF|OPID|` operation payloads. Existing worker-required local guard remains in place; no auth bypass.
- Native regression `tests/native/scanner_qr_test.cpp` round-trips an accented legacy operation QR and exercises both operation prefixes. `npm run test:scanner` passed. `./scripts/build.sh` passed (1,377,075 bytes flash; 72,172 bytes RAM); no flash, OTA package promotion, or deployment performed.
- Full `npm test` was attempted after `npm ci`: scanner regression passed; UI visual test could not start because expected `../artifacts/esp-kiosk/ui-audit/report.json` fixture is absent (Playwright reports no tests found). Node dependencies are installed from existing lockfile into ignored `node_modules`; no production credentials were read.
- Commit `fa67349` on branch `agent/codex/po6391-qr-utf8-fix`; draft PR #5 opened. Working tree clean. Full UI visual test remains blocked by the absent generated report fixture. Physical firmware version and actual scan attribution remain unverified; do not merge/deploy before validation.
