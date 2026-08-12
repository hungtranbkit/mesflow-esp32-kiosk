#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

mapfile -t INOS < <(find "$ROOT" -type f -name '*.ino' \
  ! -path '*/build/*' ! -path '*/.git/*' | sort)

if ((${#INOS[@]}==0)); then
  echo "[ERROR] Không tìm thấy .ino trong $ROOT" >&2
  exit 1
fi

if ((${#INOS[@]}>1)); then
  echo "[ERROR] Có nhiều sketch. Không tự đoán sketch cần build:" >&2
  printf '  %s\n' "${INOS[@]}" >&2
  echo "Set ESP_SKETCH_DIR trong .mesflow-arduino.env." >&2
  exit 2
fi

dirname "${INOS[0]}"
