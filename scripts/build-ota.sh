#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
[[ -f .mesflow-arduino.env ]] && source .mesflow-arduino.env
CONFIG="${MESFLOW_OTA_ENV_FILE:-${XDG_CONFIG_HOME:-$HOME/.config}/mesflow/esp-kiosk/ota.env}"
[[ -f "$CONFIG" ]] && source "$CONFIG"

FQBN="${ESP_FQBN:-esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=default_8MB,PSRAM=opi}"
SKETCH="${ESP_SKETCH_DIR:-$ROOT/esp}"
CA_FILE="${MESFLOW_OTA_CA_FILE:-${XDG_CONFIG_HOME:-$HOME/.config}/mesflow/esp-kiosk/root-ca.pem}"
DIST="$ROOT/dist"
BUILD_LOG="$(mktemp)"
CA_HEADER="$SKETCH/mesflow_ota_ca.h"
cleanup(){ rm -f "$BUILD_LOG" "$CA_HEADER"; }
trap cleanup EXIT

die(){ echo "ERROR: $1" >&2; exit "${2:-1}"; }
command -v arduino-cli >/dev/null 2>&1 || die "ARDUINO_CLI_NOT_FOUND (run ./scripts/setup-arduino.sh)"
[[ -d "$SKETCH" ]] || die "SKETCH_NOT_FOUND: $SKETCH"
[[ -f "$CA_FILE" ]] || die "OTA_CA_NOT_CONFIGURED\nPlace CA at: $CA_FILE\nOr set MESFLOW_OTA_CA_FILE=/path/to/root-ca.pem"
[[ -s "$CA_FILE" ]] || die "OTA_CA_EMPTY: $CA_FILE"

version="$(sed -n 's/^[[:space:]]*#define[[:space:]]\+FW_VERSION[[:space:]]*"\([^"]*\)".*/\1/p' "$SKETCH/mesflow_app.cpp" | head -1)"
build="$(sed -n 's/^[[:space:]]*#define[[:space:]]\+FW_BUILD[[:space:]]*"\([^"]*\)".*/\1/p' "$SKETCH/mesflow_app.cpp" | head -1)"
hardware="$(sed -n 's/^[[:space:]]*#define[[:space:]]\+HW_MODEL[[:space:]]*"\([^"]*\)".*/\1/p' "$SKETCH/mesflow_app.cpp" | head -1)"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][A-Za-z0-9.-]+)?$ ]] || die "FIRMWARE_VERSION_INVALID"
[[ -n "$build" ]] || die "FIRMWARE_BUILD_MISSING"
[[ -n "$hardware" ]] || die "HARDWARE_MODEL_MISSING"

if [[ -f "$DIST/latest.json" ]]; then
  previous="$(python3 - "$DIST/latest.json" <<'PY'
import json,sys
try: print(json.load(open(sys.argv[1])).get("version", ""))
except Exception: print("")
PY
)"
  if [[ "$previous" == "$version" && "${MESFLOW_OTA_ALLOW_VERSION_REUSE:-0}" != 1 ]]; then
    die "VERSION_REUSE: $version already exists in dist/latest.json; bump FW_VERSION"
  fi
fi

python3 - "$CA_FILE" "$CA_HEADER" <<'PY'
from pathlib import Path
import sys
ca=Path(sys.argv[1]).read_text(encoding="utf-8")
if "BEGIN CERTIFICATE" not in ca or "END CERTIFICATE" not in ca:
    raise SystemExit("OTA_CA_INVALID: expected PEM certificate")
escaped=ca.replace("\\", "\\\\").replace('"', '\\"').replace("\r", "").replace("\n", "\\n")
Path(sys.argv[2]).write_text(f'#pragma once\n#define MESFLOW_ROOT_CA_PEM "{escaped}"\n', encoding="utf-8")
PY

echo "===== ESP OTA BUILD ====="
echo "Version:  $version"
echo "Build:    $build"
echo "Hardware: $hardware"
echo "CA:       LOADED ($CA_FILE)"
arduino-cli compile --export-binaries --fqbn "$FQBN" "$SKETCH" 2>&1 | tee "$BUILD_LOG"

capacity="$(sed -n 's/.*Maximum is \([0-9][0-9]*\) bytes.*/\1/p' "$BUILD_LOG" | head -1)"
[[ "$capacity" =~ ^[0-9]+$ ]] || die "OTA_PARTITION_SIZE_UNKNOWN"
binary_candidates=()
while IFS= read -r file; do binary_candidates+=("$file"); done < <(find "$ROOT/esp/build" -type f -name '*.bin' ! -name '*.merged.bin' ! -name '*.bootloader.bin' ! -name 'boot_app0.bin' ! -name '*.partitions.bin' ! -name '*_flashed.bin' -print)
[[ "${#binary_candidates[@]}" -eq 1 ]] || die "OTA_BINARY_AMBIGUOUS: found ${#binary_candidates[@]} app binaries"
binary="${binary_candidates[0]}"
[[ "$(basename "$binary")" != *.merged.bin ]] || die "MERGED_IMAGE_NOT_ALLOWED_FOR_OTA"
size="$(stat -c '%s' "$binary")"
(( size > 0 )) || die "OTA_BINARY_EMPTY"
(( size <= capacity )) || die "OTA_BINARY_TOO_LARGE: $size > $capacity"
sha="$(sha256sum "$binary" | awk '{print $1}')"

mkdir -p "$DIST"
artifact="$DIST/esp-kiosk-$version.bin"
manifest="$DIST/esp-kiosk-$version.manifest.json"
package="$DIST/esp-kiosk-$version.ota.zip"
cp "$binary" "$artifact"
python3 - "$manifest" "$version" "$build" "$hardware" "$(basename "$artifact")" "$size" "$sha" "$capacity" <<'PY'
import json,sys
out={"version":sys.argv[2],"build":sys.argv[3],"hardware_model":sys.argv[4],"filename":sys.argv[5],"size":int(sys.argv[6]),"sha256":sys.argv[7],"ota_partition_size":int(sys.argv[8]),"ota_capable":True}
json.dump(out,open(sys.argv[1],"w",encoding="utf-8"),ensure_ascii=False,indent=2); open(sys.argv[1],"a").write("\n")
PY
cp "$manifest" "$DIST/latest.json"
rm -f "$package"
(cd "$DIST" && zip -q -j "$(basename "$package")" "$(basename "$artifact")" "$(basename "$manifest")")

echo
echo "ESP OTA BUILD PASS"
echo "Version:          $version"
echo "Build:            $build"
echo "Hardware:         $hardware"
echo "Binary:           ${artifact#$ROOT/}"
echo "OTA package:      ${package#$ROOT/}"
echo "Size:             $size bytes"
echo "SHA256:           $sha"
echo "OTA partition:    PASS ($size <= $capacity bytes)"
echo "CA:               LOADED"
echo "Ready for Deploy Agent upload: YES"
