# Kimex ESP32-S3 Kiosk v5.3.1 - OTA fleet safety

Firmware `5.3.3` adds Agent-directed OTA identity and a
dedicated OTA worker task. HTTPS is fail-closed: production builds must define
`MESFLOW_ROOT_CA_PEM` with the trusted server CA; the firmware never falls back
to `setInsecure()`.

The active build and flash procedure is documented in
`../docs/operations/ESP_KIOSK_OTA_PHASE3.md`.

## Previous worker quantity flow

## Current project structure

```text
mesflow-esp32-kiosk/
|-- esp/                    # Arduino sketch directory
|   |-- esp.ino             # Sketch entry point; open this file in Arduino IDE
|   |-- mesflow_app.h       # Shared declarations
|   `-- mesflow_app.cpp     # Firmware implementation
|-- build/                  # Local build output (not firmware source)
|-- docs/                   # Firmware and hardware documentation
|-- tools/                  # Local build/support tools
|-- AGENTS.md               # Repository-specific development rules
|-- CHANGELOG_v*.md         # Release history
`-- README.md
```

The Arduino sketch is the `esp/` directory. Keep `esp.ino`,
`mesflow_app.h`, and `mesflow_app.cpp` together in that directory. Modify the
current sketch in place; do not create a version folder or copy/rename the
`.ino` file for a version bump.

Finish uses large, sequential worker screens: `SẢN PHẨM ĐẠT` ->
`SẢN PHẨM LỖI`. Zero defects skip directly to `XÁC NHẬN`. For positive
defects the kiosk asks `1 KHÔNG, XONG` or `2 CÓ, NHẬP SỐ`; only the second
choice opens `LỖI SỬA ĐƯỢC`. Digits enter quantity, `*` removes one digit,
and `#` advances/confirms. Repairable defects must be greater than zero and
cannot exceed total defects. `Phế` is total defect minus repairable defect.
Back from confirmation returns to repairable entry when present, otherwise to
total defect entry, without clearing earlier quantities.

Backend mapping follows MESFlow semantics directly: `good_qty = Đạt`,
`defect_qty = tổng lỗi`, and `rework_qty = lỗi sửa được`. The durable
Finish journal retains the three quantities and the original finish token
across uncertain network responses.

## v5.1.8 - Runtime keypad calibration

## v5.1.8 - Hiệu chỉnh lại khi kiosk đang chạy

- Serial Monitor: gửi `keypad-calibrate` khi kiosk đang ở màn hình `READY`.
- Web Device Manager: **Hardware → Hiệu chỉnh lại keypad**.
- Kiosk tạm khóa scanner, keypad, touch và giao dịch MES trong lúc hướng dẫn bấm 12 phím.
- Mapping mới được lưu NVS và kiosk trở về `READY` mà không reboot.
- Firmware từ chối hiệu chỉnh nếu kiosk không ở `READY` hoặc có giao dịch chờ đồng bộ.

## v5.1.7 - Keypad 3x4 tự hiệu chỉnh

- Keypad 3x4 đi qua PCF8574T trên bus I2C chung: `SDA GPIO16`, `SCL GPIO15`.
- Có thể cắm bảy dây keypad vào `P0..P7` theo thứ tự bất kỳ; để trống một chân.
- Khi chưa có mapping, LCD lần lượt yêu cầu bấm `1..9`, `*`, `0`, `#`.
- Firmware tự nhận cặp chân, kiểm tra đúng cấu trúc ma trận 4 hàng × 3 cột và lưu NVS namespace `mf_keypad`.
- Mapping được giữ qua reboot và `factory-reset` thông thường.
- Gõ `keypad-calibrate` trong Serial Console, hoặc `keypad-calibrate CONFIRM` trong Web Device Manager, để hiệu chỉnh lại ngay khi kiosk đang ở `READY`.
- Không còn cần cài thư viện ngoài `I2CKeyPad`; bộ quét PCF8574T được tích hợp trực tiếp.
- Luồng sử dụng giữ nguyên: số `0..9`, `*` xóa, `#` xác nhận; màn xác nhận dùng `1/#` để lưu và `2/*` để sửa.

### Đấu PCF8574T

- `VCC` → `3.3V`
- `GND` → `GND`
- `SDA` → `GPIO16`
- `SCL` → `GPIO15`
- Bảy dây keypad → bảy chân bất kỳ trong `P0..P7`; một chân còn lại để trống.

# Lịch sử phiên bản

## MESFlow ESP32-S3 Kiosk v5.1.0 - Unified Midnight Blue UI

This package is based on v4.0.8 UX Telemetry and adds a Wi-Fi reconnect stability fix.

## Fix

- HTTP GET/POST no longer restarts Wi-Fi during a request retry.
- Short Wi-Fi status transitions under 8 seconds are treated as transient.
- Background reconnect uses `WiFi.reconnect()` without `WiFi.disconnect()`.
- Durable START/FINISH journal, UX telemetry, live activity, touch and provisioning features are preserved.

Firmware: `ESP32-KIOSK-4.0.9-WIFI-STABILITY`

# MESFlow ESP32-S3 Kiosk v4.0.8 Live Activity

Firmware identifier:

`ESP32-KIOSK-4.0.8-LIVE-ACTIVITY`

## Legacy v4 Arduino structure

The file names below describe the historical v4.0.8 package. For the current
source layout, use `esp/esp.ino` as documented at the top of this README.

All Arduino source files remain in the same sketch directory:

- `MESFlow_ESP32S3_Kiosk_v4_0_8.ino`
- `mesflow_app.cpp`
- `mesflow_app.h`

Open the `.ino` file directly with Arduino IDE.

## Main additions

- Full Station action timeline for Backend V64.2.1.
- `session_trace_id` for one kiosk interaction.
- Persistent unique `client_event_id`.
- LittleFS FIFO queue for 100 offline action events.
- Heartbeat context: worker, Operation, PO, trace and queue.
- Existing quantity-entry idle timeout and state watchdog retained.
- Action logging cannot block or change the production transaction flow.

See `LIVE_ACTIVITY_BACKEND_V64_2_1.md` for the event contract.


## Compile hotfix for ESP32 Arduino Core 3.3.11
- Added forward declaration for `addAuthHeaders(HTTPClient&)`.
- Changed action telemetry POST to `http.POST(payload)` to preserve const-correctness.

## Heartbeat diagnostic hotfix

Firmware: `ESP32-KIOSK-4.0.8-LIVE-ACTIVITY-HB-DIAG`

- Optional worker/operation/PO context is omitted when unavailable instead of sending empty values.
- Adds `po_code` to heartbeat when known.
- Prints the backend response body for non-2xx heartbeat responses.
- Saturates `heartbeatFailCount` at 255.

## Server URL bắt buộc (no hardcoded fallback)

- `SERVER_BASE` mặc định là chuỗi rỗng.
- URL chỉ được nạp từ NVS (`mesflow_cfg/server`) hoặc nhập qua Setup Portal.
- Reboot/reset giao diện giữ nguyên URL đã lưu.
- `factory-reset` và `clear-config` xóa URL; lần boot sau kiosk mở Setup Portal và không bind/heartbeat/START/FINISH cho đến khi cấu hình URL hợp lệ.
- Chấp nhận `http://` và `https://`.

## Persistent kiosk identity

Bản này tạo `device_uuid` + `device_secret` một lần trong NVS namespace `mf_identity`. Lệnh `factory-reset` chỉ xóa config/runtime và LittleFS, không xóa identity. Sau khi cấu hình lại Wi-Fi/server, firmware gọi `POST /api/kiosk/connect` để server nhận diện kiosk cũ và cấp token mới. Xem `BACKEND_PERSISTENT_KIOSK_IDENTITY.md`.

## HTTPS header fix
- POST requests now use `WiFiClientSecure` with temporary `setInsecure()`.
- Firmware no longer manually adds `Host`, `Content-Length`, `Transfer-Encoding`, or `Connection`.
- `HTTPClient` owns framing and transport headers.
- JSON POST requests add only `Content-Type`, `Accept`, `User-Agent`, plus MESFlow authentication headers.
- Production deployment should replace `setInsecure()` with `setCACert()`.

## Unified HTTP/HTTPS transport

Firmware: `ESP32-KIOSK-4.0.8-UNIFIED-NETWORK`

- All network calls use `MesHttpSession`.
- `http://...` automatically uses `WiFiClient` for LAN servers.
- `https://...` automatically uses `WiFiClientSecure`.
- Catalog GET, normal GET, JSON POST, persistent identity connect, heartbeat and action events share the same transport policy.
- `HTTPClient` generates `Host`, `Content-Length`/`Transfer-Encoding`, and `Connection`; firmware never adds them manually.
- HTTPS currently uses `setInsecure()` for compatibility testing. Replace it with CA validation for production.

## UX telemetry update

This build reports wrong-order scans and other operator interaction errors to `/api/kiosk/events`. See `BACKEND_USER_INTERACTION_TELEMETRY.md` for fields, error codes, database schema, and dashboard queries.


## v4.0.10 network serialization
Telemetry is queued and sent one event at a time only when no business transaction is active. Check Serial `[BOOT] reset_reason=...` if the KET NOI WIFI boot screen still appears.

## UART QR scanner — v4.0.16

The firmware now reads a TTL UART scanner from the board P2 connector at 9600 8N1.

```text
Scanner TX -> ESP RX / GPIO43
Scanner RX -> ESP TX / GPIO44 (optional)
Scanner GND -> GND
Scanner 5V -> 5V
```

Open USB Serial Monitor at 115200. A successful scan of `WF|EMP|NV002` prints `[SCANNER RX] WF|EMP|NV002` and is passed into the normal kiosk workflow.

## v4.1.8 Blue Unified UI
Giao dien LCD da duoc dong bo theo mau tham cong nghiep MESFlow; cac chuc nang nghiep vu khong thay doi.


## Display color fix in v5.1.0

The firmware explicitly sends ILI9341 inversion OFF after initialization and rotation. Expected production palette:

- Background: near black (`0x0021`)
- Main text: white (`0xFFFF`)
- Primary icon/action: MESFlow blue (`0x3C1F`)

At startup Serial Monitor prints `[DISPLAY] ... inversion=OFF`.


## v5.1.2 UI
- Header LCD: KIMEX.
- LED xanh chớp khi Wi-Fi và MES online; đỏ mờ khi offline.
- Dòng chờ: QUET THE NV.


## v5.1.5 - Streamlined flow

- Quét thẻ: giữ màn hình `QUET THE` trong lúc tra cứu, rồi chuyển thẳng sang nhân viên.
- Bỏ màn hình `DANG DOC THE`.
- Khi kết thúc: giữ nguyên màn hình xác nhận trong lúc gửi API.
- Bỏ các màn hình `DANG GUI` và `DANG DONG BO` trong giao dịch thông thường.
- Thiết kế lại xác nhận thành hai khối `DAT` / `LOI` và hai nút lớn `1 OK` / `2 SUA`.

## v5.1.6 - Keypad 4x4 qua PCF8574T

### Đấu dây

- `PCF8574T VCC` → `3.3V`
- `PCF8574T GND` → `GND`
- `PCF8574T SDA` → `GPIO16`
- `PCF8574T SCL` → `GPIO15`
- Keypad 4x4 nối 8 chân vào `P0..P7` của module PCF8574T.

Firmware tự dò PCF8574T trong dải `0x20..0x27`. Cảm ứng FT6336G (`0x38`) và keypad dùng chung bus I2C; không khởi tạo thêm một bus `Wire` khác.

### Cách dùng

- Màn `NHAP DAT` / `NHAP LOI`: bấm số, `*` để xóa, `#` để sang bước tiếp theo.
- Màn `XAC NHAN`: `1` hoặc `#` để lưu; `2` hoặc `*` để quay lại sửa.
- Số lượng được giới hạn từ `0` đến `999999`.

Ghi chú lịch sử: riêng bản v5.1.6 cần thư viện Arduino `I2CKeyPad`; từ v5.1.7 đã bỏ phụ thuộc này.
# mesflow-esp32-kiosk
