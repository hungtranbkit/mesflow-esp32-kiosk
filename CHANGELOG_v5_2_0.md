# ESP32 Kiosk 5.2.0 — OTA Foundation

- Identity: FW_VERSION 5.2.0, build 20260812.1430, hardware ES3C28P.
- Six-hour OTA checks with idle/offline-queue/session safety gates.
- HTTPS streaming into the inactive A/B OTA partition with size and SHA256 verification.
- ESP-IDF pending-verify boot validation and bootloader rollback support.
- Important OTA lifecycle events are reported to MESFlow.
