# M910 existing huge_app layout: serial-only application release

The 2026-11-27 approved read-only flash window verified A (MAC
84:C7:BB:79:39:84) as ESP32-S3 rev0.2, 16MiB flash and 8MiB embedded PSRAM,
with Secure Boot and Flash Encryption disabled. A complete private flash
backup was verified against the stopped device before any promotion.

Its 3072-byte partition table SHA256 is
`aaae2888c5a6a348004b5b436f47abb25ae32e72d9003902955a998eda723edd`.
Generating Arduino ESP32 3.3.11 `tools/partitions/huge_app.csv` with that
core's `gen_esp32part.py` produces exactly these bytes:

| Partition | Type/subtype | Offset | Size |
| --- | --- | --- | --- |
| nvs | data/nvs | 0x9000 | 0x5000 |
| otadata | data/ota | 0xe000 | 0x2000 |
| app0 | app/ota_0 | 0x10000 | 0x300000 |
| spiffs | data/spiffs | 0x310000 | 0xe0000 |
| coredump | data/coredump | 0x3f0000 | 0x10000 |

The table MD5 is valid. OTA metadata copies have valid CRCs, sequences 1
and 0, state UNDEFINED; the highest sequence selects the only app0. Its
first 1,425,936 bytes match the prior gated app SHA256
`ef2e649de6b2cb13e33621c061d0c326a0b77a441d20dc95ce8261b7d5b84d03`.
This establishes the observed running image, not a guessed version label.

The existing default_8MB profile remains unchanged. Its two-slot table
does **not** match A. Firmware 5.6.8 compiled for that profile was never
written to A; its frozen artifact is preserved. The previous app had
been installed application-only while retaining this legacy table.

## Build and delivery

Use the explicit verified profile `es3c28p-n16r8-m910-hugeapp-serial`:

```sh
./scripts/build-serial-release.sh es3c28p-n16r8-m910-hugeapp-serial
```

This reuses the normal release compiler and app/identity/table/capacity
verification. It emits only `app.bin` and a
`mesflow-esp-serial-artifact/1` manifest under
`dist/serial-release/stage/<profile>/`. No OTA or legacy OTA ZIP is
produced. Normal OTA builds and their verifier exclude serial-only
profiles, including this VERIFIED profile. The physical one-slot layout
must never be advertised as having an inactive OTA slot.

## Approved installation constraints

Preserve the actual bootloader, table, OTA metadata, NVS and filesystem.
Use a fresh verified full backup, exact board/idle/MAC guards and a reviewed
offline app-offset plan before writing only the frozen app. Do not use
`scripts/flash.sh`, which recompiles/uploads instead of promoting frozen
application bytes. The app must fit within 0x300000 bytes.

Keep the device stopped with `--after no-reset` while verifying the
written app and all protected flash outside its aligned erase span.
Keep the backed-up app slot for explicit USB rollback; there is no
automatic OTA rollback on this single-app layout.

On this A board, an RTS-only hard reset after the read window left ROM
DOWNLOAD mode. A separately authorized, installed esptool 5.6.0
`--before no-reset --after watchdog-reset read-mac` booted the unchanged
verified app successfully. Use the reviewed watchdog exit after all
verification, then verify actual LAN identity/version/queues and LCD.
No repeated blind resets or partition migration are part of this route.
