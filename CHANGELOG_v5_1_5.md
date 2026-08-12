# v5.1.5 - Streamlined Flow

- Bỏ màn hình tra cứu thẻ trung gian.
- Bỏ màn hình gửi kết thúc và đồng bộ trung gian trong luồng bình thường.
- Giữ màn hình hiện tại trong lúc API xử lý để tránh chớp.
- Thiết kế lại màn xác nhận: DAT/LOI tách khối, 1 OK và 2 SUA tách nút.
