#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

[[ -f .mesflow-arduino.env ]] && source .mesflow-arduino.env

FQBN="${ESP_FQBN:-esp32:esp32:esp32s3}"
SKETCH="${ESP_SKETCH_DIR:-}"

if [[ -z "$SKETCH" ]]; then
  SKETCH="$("$ROOT/scripts/find-sketch.sh")"
fi

echo "===== DÒ ESP ====="

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
  arduino-cli board list
  exit 2
fi

if ((${#PORTS[@]} > 1)); then
  echo "[ERROR] Có nhiều ESP32-S3:"
  printf '  %s\n' "${PORTS[@]}"
  echo "Không tự flash để tránh nhầm thiết bị."
  exit 3
fi

PORT="${PORTS[0]}"

echo "Đã nhận:"
echo "  PORT=$PORT"
echo "  FQBN=$FQBN"
echo "  SKETCH=$SKETCH"

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
echo "===== SERIAL ====="

sleep 2

arduino-cli monitor \
  --port "$PORT" \
  --config baudrate="${ESP_BAUD:-115200}"
