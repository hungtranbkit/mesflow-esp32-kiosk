#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

[[ -f .mesflow-arduino.env ]] && source .mesflow-arduino.env
CONFIG="${MESFLOW_OTA_ENV_FILE:-${XDG_CONFIG_HOME:-$HOME/.config}/mesflow/esp-kiosk/ota.env}"
[[ -f "$CONFIG" ]] && source "$CONFIG"

PORT="${1:-${ESP_PORT:-}}"
FQBN="${ESP_FQBN:-esp32:esp32:esp32s3}"
SKETCH="${ESP_SKETCH_DIR:-}"

[[ -n "$PORT" ]] || {
  echo "[ERROR] Phải chỉ định port rõ ràng." >&2
  echo "Ví dụ: ./scripts/flash.sh /dev/ttyACM0" >&2
  exit 2
}
[[ -e "$PORT" ]] || {
  echo "[ERROR] Không tìm thấy port: $PORT" >&2
  exit 2
}

CA_HEADER="$ROOT/esp/mesflow_ota_ca.h"
cleanup(){ rm -f "$CA_HEADER"; }
trap cleanup EXIT
if [[ -n "${MESFLOW_OTA_CA_FILE:-}" ]]; then
  [[ -s "$MESFLOW_OTA_CA_FILE" ]] || { echo "[ERROR] OTA CA không tồn tại: $MESFLOW_OTA_CA_FILE" >&2; exit 2; }
  python3 - "$MESFLOW_OTA_CA_FILE" "$CA_HEADER" <<'PY'
from pathlib import Path
import sys
ca=Path(sys.argv[1]).read_text(encoding='utf-8')
if 'BEGIN CERTIFICATE' not in ca or 'END CERTIFICATE' not in ca: raise SystemExit('OTA_CA_INVALID')
escaped=ca.replace('\\','\\\\').replace('"','\\"').replace('\r','').replace('\n','\\n')
Path(sys.argv[2]).write_text('#pragma once\n#define MESFLOW_ROOT_CA_PEM "'+escaped+'"\n',encoding='utf-8')
PY
fi

if [[ -z "$SKETCH" ]]; then
  SKETCH="$("$ROOT/scripts/find-sketch.sh")"
fi

echo "===== Thiết bị đang thấy ====="
arduino-cli board list || true

echo
echo "===== Compile trước khi flash ====="
arduino-cli compile --fqbn "$FQBN" "$SKETCH"

echo
echo "===== Xác nhận flash ====="
echo "FQBN:   $FQBN"
echo "PORT:   $PORT"
echo "Sketch: $SKETCH"
read -r -p "Flash firmware vào $PORT? [y/N] " answer
[[ "$answer" =~ ^[Yy]$ ]] || {
  echo "Đã hủy. Không flash."
  exit 0
}

arduino-cli upload \
  --port "$PORT" \
  --fqbn "$FQBN" \
  "$SKETCH"

echo
echo "[OK] Upload command completed."
echo "Tiếp theo có thể chạy:"
echo "  ./scripts/monitor.sh $PORT"
