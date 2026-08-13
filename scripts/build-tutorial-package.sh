#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE="$(cd "$ROOT/.." && pwd)"
OUT="$WORKSPACE/artifacts/esp-kiosk/tutorial"
FORCE=0
CAPTURE=""

while (($#)); do
  case "$1" in
    --force) FORCE=1 ;;
    --capture-dir) shift; CAPTURE="${1:-}" ;;
    *) echo "[ERROR] Unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

for command in python3 ffmpeg ffprobe zip sha256sum; do
  command -v "$command" >/dev/null || { echo "[ERROR] Missing local tool: $command" >&2; exit 2; }
done
[[ -f "$ROOT/esp/mesflow_app.cpp" && -f "$ROOT/tools/generate_tutorial_videos.py" ]] || {
  echo "[ERROR] Firmware/generator source is incomplete" >&2; exit 2;
}

if [[ -z "$CAPTURE" ]]; then
  CAPTURE="$(find "$ROOT/test-results" -mindepth 1 -maxdepth 1 -type d -name 'esp-ui-capture-*' -printf '%T@ %p\n' | sort -nr | head -1 | cut -d ' ' -f2-)"
fi
[[ -n "$CAPTURE" && -d "$CAPTURE" ]] || { echo "[ERROR] No framebuffer capture found; use --capture-dir" >&2; exit 2; }

VERSION="$(tr -d '\r\n' < "$ROOT/TUTORIAL_VERSION.txt")"
[[ "$VERSION" =~ ^[0-9]+(\.[0-9]+){3}$ ]] || { echo "[ERROR] Invalid tutorial version: $VERSION" >&2; exit 2; }
FIRMWARE_VERSION="$(sed -n 's/.*APP_VERSION[^\"]*"\([^"]*\)".*/\1/p' "$ROOT/esp/mesflow_app.cpp" | head -1)"
[[ -n "$FIRMWARE_VERSION" ]] || { echo "[ERROR] Cannot determine firmware version" >&2; exit 2; }

GENERATOR_FINGERPRINT="$(sha256sum "$ROOT/tools/generate_tutorial_videos.py" "$ROOT/scripts/build-tutorial-package.sh" "$ROOT/scripts/package-tutorial.sh" | sha256sum | cut -d ' ' -f1)"
FINGERPRINT="$({
  find "$ROOT/esp" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.ino' \) -print0 | sort -z | xargs -0 sha256sum
  printf '%s  generator\n' "$GENERATOR_FINGERPRINT"
  find "$CAPTURE" -type f \( -name '*.png' -o -name '*.json' \) -print0 | sort -z | xargs -0 sha256sum
} | sha256sum | cut -d ' ' -f1)"

CURRENT=""
if [[ -f "$OUT/manifest.json" ]]; then
  CURRENT="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("source_fingerprint",""))' "$OUT/manifest.json")"
fi
if [[ "$FORCE" -eq 0 && "$CURRENT" == "$FINGERPRINT" ]]; then
  echo "Incremental build: inputs unchanged; reusing validated videos. Use --force to regenerate."
  python3 - "$OUT/manifest.json" "$VERSION" "$GENERATOR_FINGERPRINT" <<'PY'
import json,sys
from datetime import datetime
path,version,generator=sys.argv[1:]
m=json.load(open(path,encoding='utf-8'))
moved=m.get('tutorial_version')!=version
m['tutorial_version']=version
m['generator_fingerprint']=generator
if moved:
    m['generated_at']=datetime.now().astimezone().isoformat(timespec='seconds')
open(path,'w',encoding='utf-8').write(json.dumps(m,ensure_ascii=False,indent=2)+'\n')
PY
else
  python3 "$ROOT/tools/generate_tutorial_videos.py" --capture-dir "$CAPTURE" --source-fingerprint "$FINGERPRINT"
fi

python3 - "$OUT/manifest.json" "$VERSION" "$FIRMWARE_VERSION" "$FINGERPRINT" <<'PY'
import json,sys
path,version,firmware,fingerprint=sys.argv[1:]
m=json.load(open(path,encoding='utf-8'))
assert m['tutorial_version']==version
assert m['firmware_version']==firmware
assert m['source_fingerprint']==fingerprint
assert m['audio'] is False
assert len(m['videos'])==7
PY

PACKAGE="$($ROOT/scripts/package-tutorial.sh "$OUT" | tail -1)"
echo "Local tutorial build PASS"
echo "Firmware: $FIRMWARE_VERSION"
echo "Tutorial: $VERSION"
echo "Fingerprint: $FINGERPRINT"
echo "Package: $PACKAGE"
echo "SHA256: $PACKAGE.sha256"
