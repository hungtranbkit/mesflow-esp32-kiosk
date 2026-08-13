#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
[[ -f .mesflow-arduino.env ]] && source .mesflow-arduino.env
OUT="${ESP_UI_AUDIT_DIR:-$ROOT/../artifacts/esp-kiosk/ui-audit}"
HOST="${ESP_UI_HOST:-192.168.1.115}"
PORT="${ESP_UI_PORT:-17892}"
mkdir -p "$OUT/playwright"
echo "ESP UI source: $ROOT/esp"
echo "ESP debug framebuffer: http://${HOST}:${PORT}"
arduino-cli compile --fqbn "${ESP_FQBN}" "$ROOT/esp" >/dev/null
python3 "$ROOT/tools/esp_ui_audit.py" --base "http://${HOST}:${PORT}" --output "$OUT"
python3 "$ROOT/tools/serve_ui_audit.py" --dir "$OUT" --port "${ESP_UI_HARNESS_PORT:-8765}" >/tmp/mesflow-esp-ui-harness.$$.log 2>&1 &
SERVER_PID=$!; trap 'kill "$SERVER_PID" 2>/dev/null || true' EXIT
for _ in {1..30}; do
  if curl -fsS "http://127.0.0.1:${ESP_UI_HARNESS_PORT:-8765}/esp-ui-test?state=ready" >/dev/null 2>&1; then break; fi
  sleep .2
done
curl -fsS "http://127.0.0.1:${ESP_UI_HARNESS_PORT:-8765}/esp-ui-test?state=ready" >/dev/null
ESP_UI_AUDIT_DIR="$OUT" ESP_UI_HARNESS_PORT="${ESP_UI_HARNESS_PORT:-8765}" npx playwright test "$ROOT/tests/ui/esp-ui-visual.spec.js" --reporter=line
echo "SCREENSHOT DIRECTORY: $OUT"
echo "CONTACT SHEET: $OUT/contact-sheet.png"
echo "REPORT JSON: $OUT/report.json"
echo "REPORT MD: $OUT/report.md"
