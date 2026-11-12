#!/usr/bin/env python3
"""Independent check of an OTA release zip from scripts/build-ota-release.sh.

Verifies, per artifact: the app.bin SHA256/size match its manifest, the
image is an ESP32-S3 *app* (not bootloader/merged), the embedded identity
marker names the same version/build/profile, the profile is VERIFIED in
profiles/*.json with identical constraints, and no UNVERIFIED profile (e.g.
Kiosk Linux) is present. Exit 0 only when every check passes.

    python3 tools/verify_ota_release.py dist/release/<pkg>.ota.zip [--profiles profiles]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import zipfile
from pathlib import Path

CONSTRAINT_KEYS = ("hardware_model", "chip", "flash_size", "psram", "partition_table_sha256", "ota_slot_size", "fqbn")


def verify(pkg: Path, profiles_dir: Path) -> list[str]:
    errors: list[str] = []
    profiles = {p.stem: json.loads(p.read_text()) for p in sorted(profiles_dir.glob("*.json"))}
    with zipfile.ZipFile(pkg) as z:
        names = set(z.namelist())
        rel = json.loads(z.read("release.json"))
        if rel.get("schema") != "mesflow-esp-ota-release/2":
            errors.append(f"RELEASE_SCHEMA {rel.get('schema')}")
        arts = rel.get("artifacts") or []
        if not arts:
            errors.append("NO_ARTIFACTS")
        expected = {pid for pid, p in profiles.items() if p.get("status") == "VERIFIED"}
        got = {a["profile_id"] for a in arts}
        if got != expected:
            errors.append(f"PROFILE_SET {sorted(got)} != VERIFIED {sorted(expected)}")
        extra = names - {"release.json"} - {a["path"] for a in arts} - {a["manifest"] for a in arts}
        if extra:
            errors.append(f"UNEXPECTED_FILES {sorted(extra)}")
        for a in arts:
            pid = a["profile_id"]
            data = z.read(a["path"])
            m = json.loads(z.read(a["manifest"]))
            prof = profiles.get(pid) or {}
            tag = f"[{pid}]"
            if prof.get("status") != "VERIFIED":
                errors.append(f"{tag} PROFILE_NOT_VERIFIED {prof.get('status')}")
            if hashlib.sha256(data).hexdigest() != m.get("sha256"):
                errors.append(f"{tag} SHA256_MISMATCH")
            if len(data) != m.get("size"):
                errors.append(f"{tag} SIZE_MISMATCH")
            if len(data) < 256 or data[0] != 0xE9 or struct.unpack_from("<H", data, 12)[0] != 9:
                errors.append(f"{tag} NOT_ESP32S3_IMAGE")
            elif struct.unpack_from("<I", data, 32)[0] != 0xABCD5432:
                errors.append(f"{tag} NOT_APP_IMAGE")
            elif data[32 + 144:32 + 176].hex() != m.get("app_elf_sha256"):
                errors.append(f"{tag} APP_ELF_SHA_MISMATCH")
            if m.get("artifact_type") != "app" or m.get("version") != rel.get("version") or m.get("build_id") != rel.get("build_id") or m.get("commit") != rel.get("commit"):
                errors.append(f"{tag} MANIFEST_RELEASE_MISMATCH")
            ident = f"MESFLOW_FW_IDENTITY:version={m.get('version')};build={m.get('build_id')};profile={pid};hw={prof.get('hardware_model')};end".encode()
            if ident not in data:
                errors.append(f"{tag} IDENTITY_MARKER_MISMATCH")
            mp = m.get("profile") or {}
            for k in CONSTRAINT_KEYS:
                if str(mp.get(k)) != str(prof.get(k)):
                    errors.append(f"{tag} PROFILE_CONSTRAINT_MISMATCH {k}")
            if len(data) > int(prof.get("ota_slot_size") or 0):
                errors.append(f"{tag} TOO_LARGE_FOR_SLOT")
            print(f"ARTIFACT {pid} size={len(data)} sha256={m.get('sha256')}")
    return errors


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("package", type=Path)
    ap.add_argument("--profiles", type=Path, default=Path(__file__).resolve().parents[1] / "profiles")
    args = ap.parse_args()
    errors = verify(args.package, args.profiles)
    for e in errors:
        print("FAIL", e)
    print("RELEASE_VERIFY", "PASS" if not errors else "FAIL", args.package.name)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
