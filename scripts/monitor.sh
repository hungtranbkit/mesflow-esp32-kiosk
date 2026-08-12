#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

[[ -f .mesflow-arduino.env ]] && source .mesflow-arduino.env

PORT="${1:-${ESP_PORT:-}}"
BAUD="${ESP_BAUD:-115200}"

[[ -n "$PORT" ]] || {
  echo "[ERROR] Phải chỉ định port. Ví dụ: ./scripts/monitor.sh /dev/ttyACM0" >&2
  exit 2
}
[[ -e "$PORT" ]] || {
  echo "[ERROR] Không tìm thấy port: $PORT" >&2
  exit 2
}

arduino-cli monitor \
  --port "$PORT" \
  --config baudrate="$BAUD"
