#!/usr/bin/env bash
# Build ONE OTA release with one app.bin per VERIFIED hardware profile
# (profiles/*.json). Output: dist/release/esp-kiosk-<version>+<build>.ota.zip
# consumed by the Deploy Agent (POST /api/esp-ota/releases). App images only:
# bootloader/partitions/merged images are never packaged (USB recovery only).
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
[[ -f .mesflow-arduino.env ]] && source .mesflow-arduino.env
SKETCH="$ROOT/esp"
OUT="$ROOT/dist/release"
CA_FILE="${MESFLOW_OTA_CA_FILE:-${XDG_CONFIG_HOME:-$HOME/.config}/mesflow/esp-kiosk/root-ca.pem}"
PROFILE_HEADER="$SKETCH/mesflow_build_profile.h"
CA_HEADER="$SKETCH/mesflow_ota_ca.h"
die(){ echo "ERROR: $*" >&2; exit 1; }
cleanup(){ rm -f "$PROFILE_HEADER"; [[ "${CA_GENERATED:-0}" == 1 ]] && rm -f "$CA_HEADER"; return 0; }
trap cleanup EXIT
command -v arduino-cli >/dev/null || die "ARDUINO_CLI_NOT_FOUND (source esp-kiosk-tools/env.sh or run scripts/setup-arduino.sh)"
[[ -z "$(git status --porcelain -- esp profiles scripts)" || "${MESFLOW_ALLOW_DIRTY:-0}" == 1 ]] || die "DIRTY_TREE: commit esp/ profiles/ scripts/ first (release identity = commit)"
version="$(sed -n 's/^#define FW_VERSION "\([^"]*\)".*/\1/p' "$SKETCH/mesflow_app.cpp" | head -1)"
fwbuild="$(sed -n 's/^#define FW_BUILD "\([^"]*\)".*/\1/p' "$SKETCH/mesflow_app.cpp" | head -1)"
commit="$(git rev-parse HEAD)"
build_id="${fwbuild}.g$(git rev-parse --short=10 HEAD)"
# Optional tag for non-release builds (e.g. MESFLOW_BUILD_TAG=staging with a
# staging CA bundle): the identity/build_id then can never be mistaken for the
# release artifact built from the same commit.
if [[ -n "${MESFLOW_BUILD_TAG:-}" ]]; then
  [[ "$MESFLOW_BUILD_TAG" =~ ^[a-z0-9-]{1,24}$ ]] || die "BUILD_TAG_INVALID"
  build_id="${build_id}.${MESFLOW_BUILD_TAG}"
fi
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "FW_VERSION_INVALID $version"
if [[ -f "$CA_FILE" ]]; then
  CA_GENERATED=1
  python3 - "$CA_FILE" "$CA_HEADER" <<'PY'
from pathlib import Path; import sys
ca=Path(sys.argv[1]).read_text()
assert "BEGIN CERTIFICATE" in ca, "OTA_CA_INVALID"
esc=ca.replace("\\","\\\\").replace('"','\\"').replace("\r","").replace("\n","\\n")
Path(sys.argv[2]).write_text(f'#pragma once\n#define MESFLOW_ROOT_CA_PEM "{esc}"\n')
PY
  echo "CA: $CA_FILE"
else
  echo "CA: built-in trust root (esp/mesflow_gts_root_r4.h); set MESFLOW_OTA_CA_FILE to override"
fi
rm -rf "$OUT/stage" && mkdir -p "$OUT/stage" "$OUT/logs"
profiles=()
for pf in "$ROOT"/profiles/*.json; do
  status="$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1])).get("status",""))' "$pf")"
  pid="$(basename "$pf" .json)"
  if [[ "$status" != VERIFIED ]]; then echo "SKIP profile $pid: status=$status (not built; Agent blocks its kiosks)"; continue; fi
  profiles+=("$pf")
done
(( ${#profiles[@]} )) || die "NO_VERIFIED_PROFILE"
for pf in "${profiles[@]}"; do
  pid="$(basename "$pf" .json)"
  fqbn="$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["fqbn"])' "$pf")"
  printf '#pragma once\n#define MESFLOW_HW_PROFILE "%s"\n#define MESFLOW_BUILD_ID "%s"\n' "$pid" "$build_id" > "$PROFILE_HEADER"
  bdir="$ROOT/dist/build/$pid"; rm -rf "$bdir"; mkdir -p "$bdir"
  log="$OUT/logs/$pid.build.log"
  echo "===== build $pid ($fqbn) ====="
  arduino-cli compile --fqbn "$fqbn" --build-path "$bdir/work" --output-dir "$bdir" "$SKETCH" 2>&1 | tee "$log"
  python3 - "$pf" "$bdir" "$log" "$OUT/stage/$pid" "$version" "$build_id" "$commit" <<'PY'
import hashlib, json, re, struct, sys
from pathlib import Path
pf, bdir, log, stage, version, build, commit = sys.argv[1:]
prof = json.load(open(pf)); bdir = Path(bdir)
app = bdir / "esp.ino.bin"; parts = bdir / "esp.ino.partitions.bin"
assert app.is_file() and parts.is_file(), "BUILD_OUTPUT_MISSING"
data = app.read_bytes()
cap = re.search(r"Maximum is (\d+) bytes", Path(log).read_text())
assert cap, "OTA_PARTITION_SIZE_UNKNOWN"
cap = int(cap.group(1))
psha = hashlib.sha256(parts.read_bytes()).hexdigest()
assert psha == prof["partition_table_sha256"], f"PARTITION_TABLE_MISMATCH {psha}"
assert cap == int(prof["ota_slot_size"]), f"OTA_SLOT_SIZE_MISMATCH {cap}"
assert len(data) <= cap, "OTA_BINARY_TOO_LARGE"
assert data[0] == 0xE9 and struct.unpack_from("<H", data, 12)[0] == 9, "NOT_ESP32S3_IMAGE"
assert struct.unpack_from("<I", data, 32)[0] == 0xABCD5432, "NOT_APP_IMAGE"
ident = f"MESFLOW_FW_IDENTITY:version={version};build={build};profile={prof['profile_id']};hw={prof['hardware_model']};end".encode()
assert ident in data, "IDENTITY_MARKER_MISSING"
elf = data[32 + 144:32 + 176].hex()
p = {k: prof[k] for k in ("profile_id","hardware_model","chip","min_chip_revision","flash_size","psram","partition_table_sha256","ota_slot_size","board","scanner_interface","fqbn")}
out = Path(stage); out.mkdir(parents=True)
(out / "app.bin").write_bytes(data)
manifest = {"schema": "mesflow-esp-ota-artifact/2", "artifact_type": "app", "version": version, "build_id": build, "commit": commit,
            "profile": p, "size": len(data), "sha256": hashlib.sha256(data).hexdigest(), "app_elf_sha256": elf,
            "config_schema": 2, "bootloader_requirement": "esp32s3-arduino-3.3.11-rollback-enabled"}
(out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(f"ARTIFACT {prof['profile_id']} size={len(data)} sha256={manifest['sha256']} app_elf_sha256={elf}")
PY
done
pkg="$OUT/esp-kiosk-${version}+${build_id}.ota.zip"
python3 - "$OUT/stage" "$pkg" "$version" "$build_id" "$commit" <<'PY'
import json, sys, zipfile
from pathlib import Path
stage, pkg, version, build, commit = sys.argv[1:]
arts = [{"profile_id": d.name, "path": f"{d.name}/app.bin", "manifest": f"{d.name}/manifest.json"} for d in sorted(Path(stage).iterdir())]
with zipfile.ZipFile(pkg, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("release.json", json.dumps({"schema": "mesflow-esp-ota-release/2", "version": version, "build_id": build, "commit": commit, "artifacts": arts}, indent=2))
    for a in arts:
        z.write(Path(stage) / a["path"], a["path"]); z.write(Path(stage) / a["manifest"], a["manifest"])
print("RELEASE", pkg)
PY
sha256sum "$pkg"
# Same app bytes, packaged for the LEGACY Deploy Agent upload (production
# 2.24.x: POST /api/esp-ota/upload accepts exactly 1 .bin + 1 *.manifest.json).
python3 "$ROOT/tools/make_legacy_ota_zip.py" "$pkg" "$OUT/legacy"
