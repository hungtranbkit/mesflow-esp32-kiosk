#!/usr/bin/env python3
"""Convert a v2 OTA release (release.json + <profile>/app.bin + manifest.json)
into the LEGACY Deploy-Agent package: one zip per profile containing exactly
  esp-kiosk-<version>-<profile>-app.bin
  esp-kiosk-<version>-<profile>.manifest.json   {version, hardware_model, filename, size, sha256, ...}
which is what the production Agent's POST /api/esp-ota/upload accepts
(ota_control._save_firmware_package: 1 .bin + 1 *.manifest.json, nothing else).

The app bytes are copied unchanged and their SHA-256 is re-verified against the
release manifest; non-app artifacts are refused. No build, no signing key.

usage: make_legacy_ota_zip.py <release.ota.zip> <out_dir>
"""
from __future__ import annotations

import hashlib
import json
import sys
import zipfile
from pathlib import Path


def _die(code: str):
    raise SystemExit(f"LEGACY_OTA_{code}")


def convert(release: Path, out_dir: Path) -> list[Path]:
    release, out_dir = Path(release), Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    made = []
    with zipfile.ZipFile(release) as z:
        rel = json.loads(z.read("release.json"))
        version = str(rel.get("version") or "")
        if not version:
            _die("VERSION_MISSING")
        for art in rel.get("artifacts") or []:
            man = json.loads(z.read(art["manifest"]))
            data = z.read(art["path"])
            if man.get("artifact_type") != "app":
                _die("NOT_APP")
            if str(man.get("version")) != version:
                _die("VERSION_MISMATCH")
            sha = hashlib.sha256(data).hexdigest()
            if sha != man.get("sha256") or len(data) != man.get("size"):
                _die("SHA256_MISMATCH")
            prof = man.get("profile") or {}
            hw = str(prof.get("hardware_model") or "")
            pid = str(prof.get("profile_id") or art.get("profile_id") or "")
            if not hw or not pid:
                _die("PROFILE_MISSING")
            stem = f"esp-kiosk-{version}-{pid}"
            binary = f"{stem}-app.bin"
            legacy = {"version": version, "hardware_model": hw, "filename": binary, "size": len(data),
                      "sha256": sha, "build_id": man.get("build_id", ""), "commit": man.get("commit", ""),
                      "profile_id": pid, "artifact_type": "app", "source_release": release.name}
            out = out_dir / f"{stem}.legacy-ota.zip"
            with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as dst:
                dst.writestr(binary, data)
                dst.writestr(f"{stem}.manifest.json", json.dumps(legacy, indent=2) + "\n")
            made.append(out)
    if not made:
        _die("NO_ARTIFACTS")
    return made


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr); return 2
    for p in convert(Path(argv[1]), Path(argv[2])):
        print(f"LEGACY_OTA {p} sha256={hashlib.sha256(p.read_bytes()).hexdigest()}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
