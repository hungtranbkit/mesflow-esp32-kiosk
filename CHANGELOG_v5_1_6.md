# v5.1.6 - I2C keypad 4x4

- Tích hợp bàn phím ma trận 4x4 qua PCF8574T, tự dò địa chỉ `0x20` đến `0x27`.
- Keypad dùng chung bus I2C của cảm ứng: `SDA GPIO16`, `SCL GPIO15`.
- Phím `0..9`: nhập tối đa 6 chữ số (`0..999999`).
- Phím `#`: xác nhận số đang nhập.
- Phím `*`: xóa số đang nhập về `0`.
- Tại màn hình xác nhận: `1` hoặc `#` để lưu; `2` hoặc `*` để sửa lại.
- Phím `A/B/C/D` chưa gán chức năng và được bỏ qua an toàn.
- Web Device Manager hiển thị trạng thái và địa chỉ PCF8574T.

## Thư viện Arduino cần cài

`I2CKeyPad` (API có `setKeyPadMode`, `loadKeyMap`, `setDebounceThreshold`).

