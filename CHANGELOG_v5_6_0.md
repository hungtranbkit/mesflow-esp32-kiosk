# v5.6.0 — Several operations at once + connection stability (2026-10-07)

## Multi-OP (server migration 0054_multi_open_session_per_employee)
- One employee may run several operations at once (max **6** on the device; the server allows one open
  session per operation). `/api/lookup` `active_sessions[]` is read in full (was: only `[0]`), plus local
  STARTs not yet answered by the server.
- Employee scan: 0 open → "QUÉT CÔNG ĐOẠN"; 1 open → straight to its quantity (unchanged); 2+ → the employee
  screen shows "ĐANG CHẠY N VIỆC" + up to 3 names, and an OP scan picks one.
- OP scan anywhere (incl. on a quantity screen): an OP the employee already runs → finish THAT one; a new OP →
  START it, the others stay open (was: "DANG CO SESSION" refusal).
- Finish targets the selected session (offline: by local session id, not "the worker's first one"); a server
  session is finished local-first even while offline.
- `WF|OPID|<id>` labels (every SETUP label) are accepted from the scanner and matched by operation id.
- Heartbeat sends `open_operations`.

## Stability
- Ghost session: an online FINISH now drops the local session record; on an online employee scan, local records
  whose START the server already answered but no longer lists (finished elsewhere) are dropped.
- Catalog auto-refresh: an empty catalog is retried after 15 min (was every 60 s inside loop(), with 15 s + 35 s
  timeouts against the 40 s task watchdog); a failed refresh waits 5 min.
- Background sync problems (no ACK, wrong ACK, transient refusal, cannot store ACK) are logged, no longer switch
  the operator's screen to ERROR. A REJECTED event is still shown.
- OTA: with an HTTPS agent and no CA in the build, the kiosk no longer polls (it could only fail, every 12–25 s,
  and posted OTA_CHECK from the second core). Build with `MESFLOW_OTA_CA_FILE` to enable OTA.

## Verified on the reference board (DEV, NV005, DEV-PO-1)
START OP1 → re-scan (1 open, quantity) → scan OP2 on the quantity screen (2nd START) → re-scan (2 open, list) →
scan OP1 → 7/0 → finish → OP1 CLOSED good=7, OP2 OPEN → re-scan (1 open) → `WF|OPID|2` → 3/0 → OP2 CLOSED good=3 →
re-scan (0 open). All events accepted by `/api/station/events/sync`.

## Scanner baud per device + log compaction (same release)
- **Scanner baud is per unit** (the GM65/GM865 keeps it in its own EEPROM; a wrong baud = total silence). New NVS
  `mesflow_cfg/scan_baud`, set live with the console command `scanner-baud <baud>` (serial or LAN console;
  allow-list 1200…115200); default stays 115200. The reference board's module is at **9600** (v2 ran it at 9600) —
  that is why the scanner "stopped scanning" after going back to v1. `status` now prints `Scanner: baud=… bytes=…`.
- **Event log compaction**: once every event is answered and the log is ≥ 48 KB it is deleted (on queue drain and
  at boot). Fixes the ~250-cycle ACK-scratch overflow (old events resent forever / "BO NHO OFFLINE DAY").

## Scanner baud auto-detect + keypad hot-plug/rewiring (same release)
- Scanner: until a valid MESFlow frame is read at the current baud, a garbage frame (unprintable / too short)
  moves to the next candidate (115200 → 9600 → 57600 → 38400 → 19200); the first valid frame stores the baud in NVS.
  Clean non-MESFlow barcodes do not trigger a switch. (The bench module turned out to be at 115200; a 9600 setting
  read 2 bytes of a 12-byte card.)
- Keypad (PCF8574T, 7 wires in any order): probed every 3 s while absent; a keypad that (re)appears is calibrated
  again (guided 12-key wizard → digits/positions normalized, matrix-shape check, saved in NVS); an unknown pair pressed
  at runtime = rewiring → recalibration when READY; 50 consecutive I2C errors re-begin the bus (as in v2), 250 →
  treated as unplugged. A saved mapping is still used at boot (no forced calibration on every power-up).

## "CÓ LỖI SỬA ĐƯỢC?" screen (user, 2026-10-07)
- Shown after DEFECT > 0 (as before, same as v2 and the web kiosk). Keys changed because repairable defects are rare:
  **`#` = KHÔNG, tiếp tục** → confirm screen (then `#` finishes); **`1` = CÓ** → repairable-quantity input (`#` to
  continue); `*` = back to DEFECT. Was 1 = KHÔNG, 2 = CÓ. Serial/console: 1 = CÓ, 2 (or 0) = KHÔNG.

## Hold `*` → recovery menu (ported from v2, user 2026-10-07)
- Hold `*`: countdown from 3 s ("ĐANG GIỮ * — MENU SAU N GIÂY"), **MENU KHÔI PHỤC at 5 s**, keep holding to **10 s →
  Wi-Fi setup portal** directly (as in v2; was: 10 s → maintenance mode). Released before 3 s, `*` is the normal
  delete/back key; released during the countdown, the screen is restored.
- Menu (works from any screen, independent of the session state): 1 Thử lại mạng (Wi-Fi reconnect + heartbeat now),
  2 Đồng bộ lại (send the queue now, refresh the catalog when idle), 3 Cài đặt Wi-Fi (portal), 4 Quay lại (also `*`/`#`),
  5 Khởi động lại, 6 Thông tin thiết bị (FW, ID, Wi-Fi/RSSI, IP, server, bind + queue, scanner baud, keypad, UUID),
  **7 Bảo trì (đổi server)** = v1's maintenance page, kept.
- Console (serial/LAN): `recovery-menu`, `key <c>` (inject a keypad key, for tests). Verified over serial: open, 6,
  any key back, 2, reopen, 4. The physical 5 s / 10 s hold needs a person at the keypad.

## Scanner baud auto-detect: silent wrong baud + hot-swap (2026-10-07)
- UART frame/break/parity errors (a scanner much slower than the UART can yield no bytes at all, only errors) now
  count as a garbage scan for the auto-detect.
- A confirmed baud is re-detected after 2 garbage scans in a row (scanner swapped while running).
- OTA: successful check every 5 min (was 12 s).
- Keypad `*` on the employee screen ("* HỦY") now cancels back to "QUÉT THẺ NHÂN VIÊN" (it was ignored: the keypad
  handler only knew quantity/confirm screens). Open sessions are not touched. Verified on the bench board (screenshot).
- Scanner frames are cut at CR/LF (the GM65 terminator), not only on a 50 ms gap: scans read in one go after a busy
  loop (HTTPS call 1–7 s) were glued into one invalid frame ("WF|EMP|NV006WF|EMP|NV007WF|EMP|NV008"), which looked like
  "the scanner does not work". The same code repeated within 3 s is handled once. Console `scanner-probe` measures the
  raw RX pin for 10 s (edges, shortest pulse -> baud estimate, UART bytes/errors) to tell wiring vs. baud vs. firmware.

## Fixed-size employee name (2026-10-07)
- The employee name no longer auto-shrinks (24 px for short names, 16 px for long ones). It is always 24 px on the
  worker screen and the "ĐANG LÀM" screen, wrapping to 2 lines (box 62 px). In the multi-OP list it is always 16 px,
  on up to 2 lines. Verified on the bench board: "Huỳnh Thị Mơ" (1 line) and "Phạm Hoàng Huyền Linh" (2 lines),
  same size.

## Instant employee name (2026-10-07)
- On an employee scan the name is drawn at once from the offline worker cache, then the HTTPS `/api/lookup` runs
  (TLS handshake per request, ~1.7 s) and picks the final screen (open OPs / quantity input). Measured on the bench
  board: name at 0.35 s after the scan (was ~2.3 s). Unknown cards still wait for the server.

## Lean telemetry (2026-10-07)
Background HTTPS calls on the UI loop made a scan wait for seconds. They are now cut down (user: "cơ chế đơn giản, giảm bớt sự kiện log"):
- `emitActionEvent` drops routine results (RECEIVED / SUCCESS / PENDING / RECOVERED). Only failures and rejections
  are queued; business data still goes through the MES API.
- `sendKioskEvent` (STUCK_STATE, ABANDONED_QTY_ENTRY) is queued instead of POSTed inline (that blocked for up to 7 s).
  `USER_FORCED_EXIT` (a `*` cancel) is not sent at all.
- The telemetry queue is sent only after READY has been idle for 20 s (`ACTION_QUEUE_IDLE_MS`).
- OTA: no `OTA_CHECK` event per poll, and LINK_READY no longer re-runs a check that is already in flight (it ran
  twice back-to-back after boot).
- Measured on the bench board after boot: heartbeat every 20 s, catalog once, one OTA check, nothing else.

## HTTPS keep-alive + TLS handshake timeout (2026-10-07)
- `MesKeepAlive mesKeepAlive` keeps one HTTP/1.1 connection to SERVER_BASE for the UI loop: `httpGetJson` /
  `httpPostJson` without `baseOverride`, the heartbeat and telemetry, through the `MesRequest` wrapper. Calls with
  `baseOverride` (OTA agent, OTA task on core 0) and the catalog download keep one-shot `MesHttpSession`s.
  Success -> `release()` (the socket stays open when the server allows it). Any error -> `drop()`, so the retry
  connects fresh. The link is also dropped after 45 s idle (`KEEPALIVE_IDLE_MS`; the 20 s heartbeat keeps it warm)
  and before the catalog download (frees ~45 KB). Keep-alive bodies are read with `getString()` (chunked-safe).
- TLS handshake timeout is 8 s (`TLS_HANDSHAKE_TIMEOUT_S`). The core default is 120 s: a stalled handshake froze
  the kiosk until the 40 s task watchdog. GET/POST socket read timeout is 15 -> 8 s, and the WDT is fed before
  each attempt.
- Measured on the bench board: lookup 0.35 s (was 1.65 s), whole employee scan 0.9 s (was 2.3 s), heartbeat
  0.2-0.4 s (was ~2.2 s), log shows `[HTTP] keep-alive REUSE`. Free heap 162 KB idle (was 207 KB), min free 78 KB.

## Audit fixes (2026-10-07)
Two read-only audits (blocking paths, logic) of `esp/mesflow_app.cpp`; confirmed findings fixed:
- Blocking / watchdog:
  - WDT fed at every new request (`MesHttpSession::begin`, `MesKeepAlive::begin`), in the boot Wi-Fi wait and
    after the LittleFS mount.
  - Catalog download: connect 5 s / response 12 s (was 15 / 35 s); the file writer feeds the WDT per chunk.
  - Keep-alive: drops the kept socket on Wi-Fi disconnect / new IP (`keepAliveInvalidated`). A body with neither
    Content-Length nor chunked encoding is read for at most 1.5 s, then the socket is closed (`readBody`), instead of
    `getString()` waiting for the server's idle timeout.
  - `countPendingOfflineEvents()` is memoized (`eventLogGeneration`); it used to re-read the whole log on every loop
    pass.
- OTA task (core 0):
  - `setUi` / `setError` refuse calls from any task but the UI loop (`onUiLoopTask`).
  - OTA-agent POSTs no longer drive the MES link state.
  - `otaIdleSafe` uses flags instead of reading the files from core 0.
  - After the download, the restart waits (up to 10 min) until the kiosk is idle again.
- Logic:
  - Any `setUi` closes the hold-`*` recovery menu. A scan or watchdog used to draw over it with the flag still set,
    so the next worker's digits went to the menu (`5` = reboot).
  - A rejected START removes its local session (ghost open OP).
  - Event-log compaction only runs with no open local session (their ACKs live in the log).
  - `selectOpenOp` clears the keypad buffer (digits carried over to the next open OP).
  - `WORKER_OK` is released after 90 s idle (`WORKER_IDLE_RELEASE_MS`), so the next person's OP scan is not booked
    to the previous worker.
  - Scanner dedupe is skipped on READY / ERROR (re-scan after `*` or an error works) and its window counts from the
    end of the lookup.
  - A stored or manually set scanner baud counts as confirmed (one noise burst no longer starts a re-detect).
  - A background sync reject no longer wipes a quantity being typed (error shown only on READY).
  - `KIOSK_BOOT` telemetry is kept.
- Verified on the bench board: boot, keep-alive REUSE, one OTA check, scan -> name 0.2 s / lookup 0.4 s, re-scan
  right after cancel is accepted. kiosk1 flashed and booted (heartbeat OK, keypad mapping kept).
- Not changed (design follow-ups, need server-side agreement):
  - (a) While the offline queue is non-empty, worker lookups use the local cache only (sessions opened elsewhere are
    invisible).
  - (b) `WF|OP|` vs `WF|OPID|` matching for local sessions (local entries have operationId 0).
  - (c) The action-queue rewrite is remove+rename (power loss in between drops telemetry).
  - (d) DNS has no timeout.
  - (e) UI-path `delay()`s (1.8 s "ĐÃ LƯU TẠM" holds) drop key presses.

## Audit follow-ups (2026-10-07)
- (a) A worker lookup asks the server whenever the link is up, even while the offline queue still holds events.
  Before, any queued event forced the cache-only path, so sessions opened on the web or another kiosk were invisible
  for 5 s after every START, or for good while one event kept failing. Two rules keep the list right:
  - `lookupQr` hides a server session that has an unanswered local FINISH (`finishPendingFor`: worker + operation
    QR or OPID). The server still lists it until that FINISH syncs.
  - It still merges unsynced local STARTs.
  - OP scans keep the local-first/offline START path while events are queued, so the server sees events in order.
- (b) Local sessions started with `WF|OPID|<id>` carry that operation id in the open-OP list, and an online FINISH
  removes the local record by QR or by OPID (`findOfflineSessionForOp`). Not covered: `WF|OP|X` vs
  `WF|OPID|<id>` for the same operation while offline, because the catalog cache has no operation ids.
- (c) Boot recovers the action queue after a power loss between remove and rename: the complete tmp file is kept.
- (d) DNS: not changed. lwIP DNS is bounded by its retry count, and the watchdog is now fed before each request.
- (e) The 0.9–1.8 s "ĐÃ LƯU TẠM" / "ĐÃ GHI NHẬN" holds: not changed. Scanner bytes are buffered (1 KB) and handled
  after the hold; a key press during the hold has no meaning there.
- Verified on the bench board: boot, scan -> name 0.2 s / lookup 0.35 s, `key *` cancel. kiosk1 flashed, bound,
  keypad kept. The pending-queue lookup path was not exercised live: it would need real START/FINISH on mesflow.net.

## Weak Wi-Fi at kiosk1: PC hotspot + fallback network (2026-10-07)
- Firmware: an optional second network (NVS `mesflow_cfg/wifi_ssid2`, `wifi_pass2`).
  - Boot tries the primary for 20 s, then the fallback.
  - At runtime, an outage of 45 s or more switches to the other network (`WIFI_SWITCH_AFTER_MS`).
  - On the fallback, an async scan every 10 min (idle READY only) moves back to the primary when it is at -70 dBm or
    better (`serviceWifiReturnToPrimary`).
- New serial-only command:
  `wifi-set {"ssid","password","fallback_ssid","fallback_password"}`, then `restart`. `status` shows `WiFi2`.
- `[SERIAL RX]` no longer echoes `provision` / `wifi-set` lines, which carry a token or password.
- kiosk1 PC (wired, Wi-Fi card unused): NetworkManager hotspot `KIOSK1-ESP`, 2.4 GHz channel 6, WPA2,
  `ipv4.method shared` (10.42.0.0/24, NAT), autoconnect priority 10. The default route stays on Ethernet.
  The password is in `~/esp-backups/kiosk1-esp/provision/hotspot.json` (0600, dell).
- ESP kiosk1: primary KIOSK1-ESP, fallback KIMEXVN-OFFICE. Measured: RSSI -42 dBm (was -81), heartbeat OK with
  keep-alive reuse, employee scan done in 1.0 s.

## Up to 3 known Wi-Fi networks + auto-switch (2026-10-07)
- Firmware knows up to 3 networks: `WIFI_SSID` plus NVS `wifi_ssid2/3` and `wifi_pass2/3`, in priority order.
  - `wifi-set` takes `fallback_ssid/_password` and `fallback2_ssid/_password`.
  - `wifi-scan` lists the 2.4 GHz networks in range with RSSI and channel.
- Choice (`pickKnownNetwork`): the first known network, in priority order, at -70 dBm or better; otherwise the
  strongest known network in range. This is used:
  - at boot, after one scan (the other networks are then tried in order);
  - after a 45 s outage (scan, then switch);
  - every 10 min on a lower-priority network (async scan on an idle READY screen), to move back up.
- `wifiBeginSlot` stops a pending (auto-)connect first and checks with `esp_wifi_get_config` that the driver took
  the new SSID. While the driver is reconnecting, a new config is refused silently: first test on kiosk1, the ESP
  "switched" to OFFICE but stayed on the hotspot config. After every (re)connect the slot is re-read from
  `WiFi.SSID()`.
- kiosk1 failover test: hotspot off at t=5 s -> outage -> switch to KIMEXVN-OFFICE at 57 s, connected at 59 s
  (-78 dBm), heartbeats OK.
- Dell bench ESP: 1 = Airport, 2 = KIOSK1-ESP, 3 = KIMEXVN-OFFICE. It can be moved to the kiosk1 area without setup.
