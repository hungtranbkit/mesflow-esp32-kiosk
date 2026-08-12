# v5.1.8 - Runtime keypad calibration

- Cho phép hiệu chỉnh lại keypad ngay khi firmware đang chạy, không reboot ESP32.
- Serial command: `keypad-calibrate`.
- Web Device Manager: `keypad-calibrate CONFIRM` qua nút **Hiệu chỉnh lại keypad**.
- Chỉ cho bắt đầu khi kiosk ở `READY`, không có pending transaction và PCF8574T đang online.
- Tạm khóa scanner, keypad, touch, watchdog và MES background work trong quá trình hiệu chỉnh.
- Xóa frame scanner cũ trước/sau hiệu chỉnh để tránh nhận nhầm QR.
- Lưu mapping mới vào NVS, reset bộ đệm keypad và quay lại màn hình `READY`.
