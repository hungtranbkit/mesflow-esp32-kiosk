# Wi-Fi setup cancellation checkpoint — 2026-11-28

Implementation commit: `81bb5f6845a0c8053c05ad765d863113e42b2d19`.

## Baseline reconciliation

Fresh origin refs were checked before editing and again before publishing:

| Ref | Commit | Disposition |
| --- | --- | --- |
| main | 92a80d4ce6ce3c5f169a7d7798ed2309c856b085 | Not the deployed v5.6.10 baseline |
| PR #4 merge / fix/deepsleep-ota-gate-20261124 | c0a80ce3d3c8888ffd07ddce5e175eed2636eeb4 | This fix's base; tree matches merged v5.6.10 head 9ad8c963cddceb1ea58271aeb6d6aea5154ba817 |
| OTA PR #2 | 2843558f96a030e7e31f3eefc3edc948d43fc46f | Open, left separate; merge-base 09db474c78dfb6223ed91c07e92c80db85c86677 |
| Scanner PR #5 | 81f755a802fea248e97db07c1864aa9e6b529b4a | Draft, left separate; merge-base is main above |

PR #4 merged into the deep-sleep branch, not main. This dedicated branch targets that merge, preserving display-number, identity, sleep and OTA changes. It neither merges pending PRs nor adds multi-network Wi-Fi support. Work used a new bare repository and isolated worktree; existing checkouts were untouched.

## Behavior

The setup-only loop now polls a bounded, debounced Back touch target and keypad `#`, advertised on LCD as `#: Quay lại`. Both inputs require release after entry and cancel; `*` has no setup action. Production scanner/touch/key handlers are excluded while setup is active, including the entry/cancel loop iterations. Scanner bytes during setup are discarded.

Cancel stops DNS/AP/setup HTTP, invalidates its phone nonce, restores STA asynchronously using existing credentials, and returns directly to OFFLINE with the recovery hint. Successful reconnection can return the idle screen to READY. No configured network also exits immediately. There is no reboot or NVS/config/journal write on cancellation. Pending offline events remain; active transactions/workflows and OTA block entry, with a shared OTA/sleep/setup gate for cross-core ownership.

Phone cancellation is scoped to the active physical setup session, AP interface and unpredictable per-session nonce. The page does not render the stored Wi-Fi password. Blank password retains it for the unchanged network; an explicit open-network checkbox supports saving an unsecured network. Existing save validation, persistence and restart remain. Setup and LAN HTTP routes register only once across repeated restarts.

## Evidence

- 60 Python tests passed in a disposable container restricted to one CPU, 512 MiB, read-only source and no network. Includes real extracted C++ lifecycle/save/registration blocks, keypad/touch bounds/debounce/release, no-config escape, 25 repeat cycles, no cancel writes, offline queue, credential/identity/display, serial release and OTA regressions.
- Eight C++ host suites passed with `-Wall -Wextra -Werror`, including scanner REST policy and 1,000 threaded OTA/sleep/setup races.
- The exact pending PR #5 scanner test was separately extracted and passed. This is isolated evidence, not proof of an integrated PR #5 build.
- New LAN route test failed before the guard and passed after it. Initial missing-cancel regressions also failed on baseline.
- Real Chromium agent-browser exercised the generated phone page with synthetic values and local mock endpoints. Cancel issued an empty body with the session header and displayed confirmation. No console/page errors were returned. This is not a real captive-portal/radio acceptance test.
- Phone layout has no horizontal overflow at 375x812, 1366x768 and 1920x1080. Buttons are 44px high. Accessible labels/status and keyboard focus (3px #103f68) checked; no animation is present. Measured body/secondary/hint/button contrast exceeds 4.5:1; input border 3.58:1 and focus 10.86:1. LCD Back text/border contrast 19.51:1; real Vietnamese font glyph bounds fit the button.
- Before/after screenshots and test logs are retained locally in `/tmp/esp-wifi-cancel-evidence-20261128`; independent source-review artifacts in `/tmp/compound-engineering-1000/ce-code-review/wifi-cancel-20261128`. Review resolved same-SSID password preservation and retained LAN-handler allocation findings. Cross-model Claude review could not authenticate; local independent review covered the fallback.

## Limits and release gate

No board was flashed, no OTA rollout was submitted, no PR was merged and no production database was used. Real LCD/touch/keypad, STA reconnection, repeated live HTTP/DNS teardown, exact on-device NVS/journal preservation and canary acceptance remain required, followed by independent validation. Host fakes cannot establish these hardware properties.

HP became overloaded and browser checks initially stalled; the browser later recovered and the fixture cancel flow passed. No heavy firmware build or file cleanup was run on HP (9.5 GiB free, 96% used at checkpoint). M910 SSH and pinned core 3.3.11/libraries are reachable, but its account cannot run Docker and sudo requires authentication. Existing remote worktrees were not changed. Use the repository's isolated GitHub CI build for this commit; record its outcome below when available.

The PR remains draft until the hardware gate passes. Historical tests and CLI submission acknowledgements do not establish current operational success.
