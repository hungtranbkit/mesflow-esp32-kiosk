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

command -v arduino-cli >/dev/null 2>&1 || {
  echo "[ERROR] Arduino CLI chưa cài. Chạy: ./scripts/setup-arduino.sh" >&2
  exit 1
}

echo "===== MESFlow ESP Build ====="
echo "FQBN:   $FQBN"
echo "Sketch: $SKETCH"
echo

arduino-cli compile \
  --fqbn "$FQBN" \
  "$SKETCH"
