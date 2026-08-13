#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE="$(cd "$ROOT/.." && pwd)"
SOURCE_DIR="${1:-$WORKSPACE/artifacts/esp-kiosk/tutorial}"
VERSION_FILE="$ROOT/TUTORIAL_VERSION.txt"

[[ -f "$VERSION_FILE" ]] || { echo "[ERROR] Missing $VERSION_FILE" >&2; exit 2; }
VERSION="$(tr -d '\r\n' < "$VERSION_FILE")"
[[ "$VERSION" =~ ^[0-9]+(\.[0-9]+){3}$ ]] || { echo "[ERROR] Invalid tutorial version: $VERSION" >&2; exit 2; }
[[ -d "$SOURCE_DIR" ]] || { echo "[ERROR] Missing tutorial output: $SOURCE_DIR" >&2; exit 2; }

python3 - "$SOURCE_DIR" "$VERSION" <<'PY'
import json, subprocess, sys
from pathlib import Path

root=Path(sys.argv[1]).resolve(); version=sys.argv[2]
manifest=json.loads((root/'manifest.json').read_text(encoding='utf-8'))
assert manifest.get('type')=='esp-kiosk-tutorial', 'manifest.type is invalid'
assert manifest.get('tutorial_version')==version, 'tutorial version mismatch'
assert manifest.get('audio') is False, 'tutorial must be silent'
videos=manifest.get('videos')
assert isinstance(videos,list) and len(videos)==7, 'exactly seven videos are required'
expected={f'{i:02d}_{name}.mp4' for i,name in enumerate(('kiosk_overview','kiosk_boot_connect','kiosk_start_session','kiosk_finish_good_qty','kiosk_defect_rework','kiosk_common_errors','kiosk_offline_reconnect'))}
names={v.get('filename') for v in videos}
assert names==expected, f'unexpected video set: {sorted(names)}'
for item in videos:
    assert all(item.get(k) not in (None,'',[]) for k in ('filename','title','description','cases'))
    assert isinstance(item.get('order'),int) and isinstance(item.get('duration_seconds'),(int,float))
    assert item.get('id'), 'video id is required'
    path=root/'videos'/item['filename']; assert path.is_file() and path.stat().st_size>0
    probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_entries','stream=codec_type,width,height:format=duration','-of','json',str(path)],text=True))
    video=[stream for stream in probe.get('streams',[]) if stream.get('codec_type')=='video']
    assert len(video)==1, f'{path.name}: expected one video stream'
    assert int(video[0].get('width') or 0)>0 and int(video[0].get('height') or 0)>0, f'{path.name}: invalid resolution'
    assert float(probe.get('format',{}).get('duration') or 0)>0, f'{path.name}: invalid duration'
    audio=subprocess.check_output(['ffprobe','-v','error','-select_streams','a','-show_entries','stream=index','-of','csv=p=0',str(path)],text=True).strip().splitlines()
    assert len(audio)==0, f'{path.name}: expected zero audio streams'
print('Tutorial source validation PASS')
PY

STAGING="$(mktemp -d)"
trap 'find "$STAGING" -mindepth 1 -delete 2>/dev/null || true; rmdir "$STAGING" 2>/dev/null || true' EXIT
PACKAGE_ROOT="$STAGING/esp-kiosk-tutorial"
mkdir -p "$PACKAGE_ROOT/videos"
printf '%s\n' "$VERSION" > "$PACKAGE_ROOT/VERSION.txt"
cp "$SOURCE_DIR/manifest.json" "$PACKAGE_ROOT/manifest.json"
for video in "$SOURCE_DIR"/videos/*.mp4; do cp "$video" "$PACKAGE_ROOT/videos/"; done

OUT_DIR="$WORKSPACE/artifacts/esp-kiosk/packages"
mkdir -p "$OUT_DIR"
PACKAGE_VERSION="${VERSION//./_}"
OUT="$OUT_DIR/ESP_Kiosk_Tutorial_${PACKAGE_VERSION}.zip"
(cd "$STAGING" && zip -q -r "$OUT" esp-kiosk-tutorial)
python3 - "$OUT" <<'PY'
import zipfile,sys
with zipfile.ZipFile(sys.argv[1]) as z:
    roots={x.filename.replace('\\','/').split('/',1)[0] for x in z.infolist() if x.filename}
    assert roots=={'esp-kiosk-tutorial'}, roots
    files=[x.filename.replace('\\','/') for x in z.infolist() if not x.is_dir()]
    assert len(files)==9
    assert sum(name.startswith('esp-kiosk-tutorial/videos/') and name.endswith('.mp4') for name in files)==7
print('ZIP contract validation PASS')
PY
(cd "$OUT_DIR" && sha256sum "$(basename "$OUT")" > "$(basename "$OUT").sha256")
echo "$OUT"
