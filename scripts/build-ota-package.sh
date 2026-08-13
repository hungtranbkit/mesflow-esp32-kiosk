#!/usr/bin/env bash
set -Eeuo pipefail

# The Agent calls this script from the ESP source tree.  Keep the Arduino
# details in build-ota.sh and only standardise the portable promotion package.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$ROOT/scripts/build-ota.sh"

version="$(sed -n 's/^[[:space:]]*#define[[:space:]]\+FW_VERSION[[:space:]]*"\([^"]*\)".*/\1/p' "$ROOT/esp/mesflow_app.cpp" | head -1)"
build="$(sed -n 's/^[[:space:]]*#define[[:space:]]\+FW_BUILD[[:space:]]*"\([^"]*\)".*/\1/p' "$ROOT/esp/mesflow_app.cpp" | head -1)"
hardware="$(sed -n 's/^[[:space:]]*#define[[:space:]]\+HW_MODEL[[:space:]]*"\([^"]*\)".*/\1/p' "$ROOT/esp/mesflow_app.cpp" | head -1)"
source_commit="$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
built_at="$(date -Is)"
dist="$ROOT/artifacts/esp-kiosk/ota"
mkdir -p "$dist"
bin_src="$ROOT/dist/esp-kiosk-$version.bin"
manifest_src="$ROOT/dist/esp-kiosk-$version.manifest.json"
[[ -s "$bin_src" && -s "$manifest_src" ]] || { echo "PACKAGE_INPUT_MISSING" >&2; exit 1; }
size="$(stat -c '%s' "$bin_src")"
sha="$(sha256sum "$bin_src" | awk '{print $1}')"
name="ESP_Kiosk_$version"
bin_out="$dist/$name.bin"
json_out="$dist/$name.json"
sha_out="$dist/$name.sha256"
zip_out="$dist/${name//./_}_OTA.zip"
cp "$bin_src" "$bin_out"
python3 - "$json_out" "$version" "$build" "$hardware" "$(basename "$bin_out")" "$size" "$sha" "$source_commit" "$built_at" <<'PY'
import json,sys
out={"type":"esp-kiosk-firmware","version":sys.argv[2],"build":sys.argv[3],"board":sys.argv[4],"chip":"ESP32-S3","flash":"16M","psram":"opi","filename":"firmware.bin","size":int(sys.argv[6]),"sha256":sys.argv[7],"built_at":sys.argv[9],"source_commit":sys.argv[8]}
json.dump(out,open(sys.argv[1],"w",encoding="utf-8"),ensure_ascii=False,indent=2);open(sys.argv[1],"a").write("\n")
PY
printf '%s  firmware.bin\n' "$sha" > "$sha_out"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/esp-kiosk-firmware"
printf '%s\n' "$version" > "$tmp/esp-kiosk-firmware/VERSION.txt"
cp "$bin_out" "$tmp/esp-kiosk-firmware/firmware.bin"
cp "$json_out" "$tmp/esp-kiosk-firmware/manifest.json"
cp "$sha_out" "$tmp/esp-kiosk-firmware/SHA256SUMS"
rm -f "$zip_out"
(cd "$tmp" && zip -qr "$zip_out" esp-kiosk-firmware)
echo "OTA PACKAGE PASS"
echo "Version: $version"
echo "Build: $build"
echo "Binary: $bin_out"
echo "Size: $size bytes"
echo "SHA256: $sha"
echo "Package: $zip_out"
