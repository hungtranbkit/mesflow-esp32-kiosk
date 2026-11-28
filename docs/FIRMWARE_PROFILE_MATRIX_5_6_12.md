# ESP32 firmware profile matrix — 5.6.12 candidate

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

Firmware version `5.6.12` advances past the frozen local 5.6.11 candidate
(`b18cc4b`, app SHA `e46c5bd41d3ee8db984d43ead663f315966eb09c366c86240c3e38c7cad9c835`).
Each 5.6.12 profile is built from one source commit, with profile-specific
partition identity and artifact checksums recorded in the release handoff.
