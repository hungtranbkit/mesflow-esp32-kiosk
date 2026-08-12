# v5.1.2 - KIMEX + Network Blink

- Đổi thương hiệu trên header LCD từ `MESFLOW` thành `KIMEX`.
- Rút gọn lời nhắc `QUET THE NHAN VIEN` thành `QUET THE NV`.
- Thêm LED mạng trên header:
  - Chớp xanh mỗi 500 ms khi Wi-Fi kết nối và MES đang online.
  - Đỏ mờ khi mất Wi-Fi hoặc MES offline.
- Dời LED sang vị trí riêng để không bị vùng đồng hồ vẽ đè.
- Giữ nguyên panel inversion ON, UART scanner, Web Console và catalog QR demo.
