#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

[[ -f .mesflow-arduino.env ]] && source .mesflow-arduino.env

FQBN="${ESP_FQBN:-esp32:esp32:esp32s3}"
BAUD="${ESP_BAUD:-115200}"
SKETCH="${ESP_SKETCH_DIR:-}"

if [[ -z "$SKETCH" ]]; then
  SKETCH="$("$ROOT/scripts/find-sketch.sh")"
fi

command -v arduino-cli >/dev/null 2>&1 || {
  echo "[ERROR] Arduino CLI chưa cài."
  echo "Chạy: ./scripts/setup-arduino.sh"
  exit 1
}

echo "===== DÒ ESP32-S3 ====="

mapfile -t PORTS < <(
  arduino-cli board list |
    awk '
      /esp32:esp32:esp32s3/ ||
      /ESP32-S3/ ||
      /USB JTAG/ {
        print $1
      }
    ' |
    grep '^/dev/' |
    sort -u
)

if ((${#PORTS[@]} == 0)); then
  echo "[ERROR] Không tìm thấy ESP32-S3 phù hợp."
  echo
  arduino-cli board list || true
  exit 2
fi

if ((${#PORTS[@]} > 1)); then
  echo "[ERROR] Có nhiều ESP32-S3 đang kết nối:"
  printf '  %s\n' "${PORTS[@]}"
  echo
  echo "Dừng để tránh flash nhầm thiết bị."
  exit 3
fi

PORT="${PORTS[0]}"

echo
echo "===== THIẾT BỊ ĐÃ XÁC ĐỊNH ====="
echo "PORT:   $PORT"
echo "FQBN:   $FQBN"
echo "SKETCH: $SKETCH"

echo
echo "===== BUILD ====="

arduino-cli compile \
  --fqbn "$FQBN" \
  "$SKETCH"

echo
echo "===== FLASH ====="

arduino-cli upload \
  --port "$PORT" \
  --fqbn "$FQBN" \
  "$SKETCH"

echo
echo "[OK] FLASH THÀNH CÔNG"

echo
echo "===== SERIAL BOOT LOG ====="

sleep 2

timeout 15s \
  arduino-cli monitor \
    --port "$PORT" \
    --config baudrate="$BAUD" \
  || true

echo
echo "===== HOÀN TẤT ====="
echo "Port: $PORT"
echo "FQBN: $FQBN"
echo "Đã compile + flash + đọc serial trong 15 giây."
