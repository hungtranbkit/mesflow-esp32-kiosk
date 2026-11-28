# ESP32 firmware profile matrix — 5.6.14 candidate

Release branch: `integration/esp-canonical-release-20261128`, rooted at
`origin/main` and integrating the 5.6.11 scanner/Wi-Fi-cancel candidate. This
matrix separates source-build profiles from physical device identity. A
profile being buildable does not identify a target or authorize a write.

| Identity | Evidence on record | Hardware / partition certainty | Build/delivery decision |
| --- | --- | --- | --- |
| Kimex Máy 1, UUID `fc0c33a9-2f76-424f-a256-217699b57712` | Production heartbeat/inventory in the 2026-11-28 release checkpoint: ES3C28P, firmware 5.6.6. | Exact MAC, LCD wiring, flash table and app slots are not tied to this UUID. A test inventory `ota_capable` field is not sufficient evidence. | No device-specific image assigned; no OTA/serial write. |
| Kimex Máy 2 / A, UUID `0f2d2ad3-06d7-417f-a1e7-da3d2457db39`, MAC `84:C7:BB:79:39:84` | Accepted hardware receipt and verified flash table in `docs/M910_HUGE_APP_SERIAL.md`; heartbeat later reported 5.6.10. | ES3C28P, ILI9341 240x320 / GM65 UART1 GPIO44; `huge_app`, one 3 MiB app at `0x10000`, partition SHA `aaae2888c5a6a348004b5b436f47abb25ae32e72d9003902955a998eda723edd`; no inactive OTA app slot. | `es3c28p-n16r8-m910-hugeapp-serial` app-only artifact only. It is not an OTA image and is not installed by this task. |
| Historical M910 inventory UUID `2eebb378-e0dd-4c3b-aa4d-39ae012437e0` | Read-only inventory last seen 2026-11-11, reported 5.6.2; not tied to the Nov 28 incident. | Physical MAC/partition table not freshly verified. Repo has two distinct VERIFIED M910 profiles: default_8MB dual-slot and huge_app single-slot, which are not interchangeable. | Build both repo-VERIFIED profile artifacts; target remains unverified. No device write. |
| Dell kiosk1 USB reference, MAC `28:84:85:85:75:10` | Read-only USB metadata; prior board docs identify it as Kiosk Linux reference. | Repo profile `es3c28p-n16r8-kiosklinux-gm65uart1` is explicitly `UNVERIFIED`; no partition-table receipt or incident UUID mapping. | Builder must skip it. No serial port opened, no reset/flash. |

The default_8MB M910 profile is the only dual-slot profile marked VERIFIED by
the repository and is built as a candidate OTA artifact. Its presence does
not establish that the historical M910 inventory unit currently has that
table. No Kimex Máy 1 profile is built until its actual LCD, flash, and
partition evidence is joined to its UUID. The two Kimex units therefore do
not inherit an M910 binary by model-name similarity.

Firmware version `5.6.14` advances past the frozen local 5.6.11 candidate
(`b18cc4b`, app SHA `e46c5bd41d3ee8db984d43ead663f315966eb09c366c86240c3e38c7cad9c835`).
The intermediate local 5.6.12 compile used unpinned Dell libraries and is
superseded; its app/manifest/ZIP were preserved under
`task-artifacts/po6391-diagnosis-20261128/retired-v5612-unpinned/` and are not
release inputs.

## Build artifacts

Source commit `dcd1deb05ba90eb1227f24fb1e2c4ce5a3a65f2a`; build ID
`20261128.0004.gdcd1deb05b`. Local build used ESP32 core 3.3.11, GFX 1.13.0,
ILI9341 1.7.0, BusIO 1.17.4, and ArduinoJson 6.21.5 in an isolated Arduino
user library directory. Built-in GTS Root R4 was used; no OTA auth secret or
device credential was included.

| Profile | Delivery / capacity | App size / SHA-256 | Package SHA-256 |
| --- | --- | --- | --- |
| `es3c28p-n16r8-m910-gm65uart1` | OTA; `default_8MB`, 3,342,336-byte slots; partition SHA `1d9cca96de0fe07ad7fc0648b9878ddecd9ce565e38b589ad20fea698ed4c80c` | 1,440,096 bytes; `717bf44df75a8e39176e56d94d2bc302adcdff976202b07ec17ae05a85a30de6` | ZIP `002ca40437db54b96888dadd00ae0eceb35cc917340262d00a616bd87075c8b1` |
| `es3c28p-n16r8-m910-hugeapp-serial` | Serial app only; one 3,145,728-byte app at `0x10000`; partition SHA `aaae2888c5a6a348004b5b436f47abb25ae32e72d9003902955a998eda723edd` | 1,440,112 bytes; `c45af61c2273996d856f91e79e2ad32dc989d3f9abb90278d2aed27418bba512` | No OTA ZIP by design; manifest SHA recorded in release manifest |

Release ZIP verification passed. Artifacts and manifests were copied under
`/home/dell/workspace/task-artifacts/po6391-diagnosis-20261128/firmware-canonical-5.6.14/`; release-manifest SHA `87f6e40361920723be0c2daf7424daf22475e6b51e439fa6e393f753c828982f`.
`WORKER_REQUIRED`, UTF-8 scanner and OPID handling, setup cancel, OTA rollback,
approval, numeric ID, GM65/keypad and deep-sleep interlocks remain covered by
the local tests. New lineage regressions cover CR/LF scanner framing, worker
selection cancel, offline OPID reconciliation and action-queue power-loss
recovery. No hardware write or device-specific Kimex build was made. PR #9
host-tests and build-release checks pass on `dcd1deb`; review remains pending.
Each 5.6.14 profile is built from one source commit, with profile-specific
partition identity and artifact checksums recorded in the release handoff.
