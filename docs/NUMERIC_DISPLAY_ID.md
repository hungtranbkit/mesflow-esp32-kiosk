# Numeric device display IDs — firmware 5.6.10

## Contract

MESFlow owns the optional authenticated response field `display_number`:
JSON integer 1..2147483647. Render decimal with minimum width two (01, 02,
99, 100); never wrap, truncate, infer from a name/UUID, or allocate locally.
Initial assignment: Máy 1 / B = 1; Máy 2 / A = 2. UUID, secret, kiosk token,
station and enrollment challenge remain unchanged.

## Persistence and rendering

- Keep one versioned 240-byte NVS blob at `mesflow_cfg/display_num`:
  uint32 version + uint32 number + server[192] + full UUID[40]. All fields
  are initialized; a static size assertion prevents an accidental layout change.
- Normalize the configured server with the existing credential policy's ASCII
  case and trailing-slash rules. The cached record is usable only for that
  server and exact full UUID. This is a display identifier, never authentication.
- Write the entire record and read it back before publishing to RAM/display.
  Missing, invalid or unchanged values do not write NVS. A failed save remains
  retryable. Old-server omitted fields retain an existing same-server cache.
- Load the record at boot, validating schema, length, range and bounded strings.
  No valid current-scope record means `ID --`. One cache record does not keep
  a history for every server; returning to an overwritten scope needs a fresh
  authenticated response. Merely switching scope does not erase the record.
- Consume at successful bind and valid successful heartbeat. Header updates
  reuse the existing deferred idle repaint and its worker/quantity/queue/OTA
  guards; preserve the Vietnamese name box, clock and network indicator.

## Verification and release sequence

1. Run the existing focused baseline and observe the old LCD fail the new
   numeric-ID regression.
2. Implement policy, single-record persistence, response intake and header;
   verify invalid JSON types/ranges, NVS failures/readback, reboot, server/UUID
   scope, 100+, repeated response no-ops and active-workflow deferred repaint.
3. Run the existing host/Python gates and actual-font header bounds tests.
   The device-mutating visual suite is not run on working production hardware.
4. Review a clean commit before the one pinned full release compile.
5. Use the already qualified `es3c28p-n16r8-m910-hugeapp-serial` profile for A:
   huge_app table aaae2888c5a6a348004b5b436f47abb25ae32e72d9003902955a998eda723edd,
   app offset 0x10000, capacity 0x300000. No OTA ZIP or repartitioning.
6. Freeze version 5.6.10 / build 20261127.0004 and review artifact/target before
   any physical action. Preserve the accepted 5.6.9 artifact and rollback.

Implementation base: merged d425c8949fc9da113bd1b491522f39609752259c, same tree
as accepted 813df3e065. Owned branch: agent/codex/esp-display-number-v5610-20261127.
