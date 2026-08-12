# v5.1.7 - Auto-calibrated keypad 3x4

- Thay `I2CKeyPad` bằng bộ quét cặp chân PCF8574T tích hợp trong firmware.
- Cho phép bảy dây keypad 3x4 cắm vào `P0..P7` theo thứ tự bất kỳ.
- Thêm hướng dẫn hiệu chỉnh 12 phím trực tiếp trên LCD khi chưa có mapping.
- Kiểm tra mapping phải tạo đúng ma trận 4x3 trước khi lưu.
- Lưu mapping trong NVS namespace `mf_keypad`; reboot và factory reset cấu hình không làm mất.
- Thêm lệnh Serial `keypad-calibrate` và lệnh Web Console `keypad-calibrate CONFIRM`.
- Web Device Manager báo trạng thái `READY`, `NEEDS CALIBRATION` hoặc `NOT FOUND`.
- Giữ nguyên luồng nhập số hiện có của kiosk.
- Sửa lỗi biên dịch hai lời gọi `printCenteredFit()` thừa tham số trong màn `XAC NHAN`.
- Loại bỏ phụ thuộc thư viện Arduino `I2CKeyPad`.
