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
