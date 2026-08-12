#!/usr/bin/env bash
set -Eeuo pipefail

echo "===== Arduino board list ====="
arduino-cli board list || true

echo
echo "===== Serial devices ====="
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || echo "Không thấy /dev/ttyACM* hoặc /dev/ttyUSB*"

echo
echo "===== USB devices potentially related to ESP/UART ====="
if command -v lsusb >/dev/null 2>&1; then
  lsusb | grep -Ei 'Espressif|CP210|CH340|CH341|USB JTAG|UART' || true
fi

echo
echo "Không tự chọn port nếu có nhiều thiết bị."
