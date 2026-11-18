"""Negative/positive tests for tools/verify_ota_release.py (synthetic images)."""
from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import verify_ota_release as v  # noqa: E402

PROF = {"profile_id": "p-ok", "status": "VERIFIED", "hardware_model": "ES3C28P", "chip": "esp32s3", "flash_size": "16MB",
        "psram": "8MB-OPI", "partition_table_sha256": "ab" * 32, "ota_slot_size": 4096, "fqbn": "esp32:esp32:esp32s3"}


def app_image(pid="p-ok", magic=0xABCD5432, first=0xE9):
    b = bytearray(1024)
    b[0] = first; b[12] = 9
    b[32:36] = magic.to_bytes(4, "little")
    b[176:208] = bytes(range(32))
    ident = f"MESFLOW_FW_IDENTITY:version=5.6.3;build=B1;profile={pid};hw=ES3C28P;end".encode()
    b[300:300 + len(ident)] = ident
    return bytes(b)


class VerifyRelease(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.profiles = self.tmp / "profiles"; self.profiles.mkdir()
        (self.profiles / "p-ok.json").write_text(json.dumps(PROF))
        (self.profiles / "p-linux.json").write_text(json.dumps({"profile_id": "p-linux", "status": "UNVERIFIED"}))

    def pkg(self, arts):
        path = self.tmp / "r.zip"
        with zipfile.ZipFile(path, "w") as z:
            rel = {"schema": "mesflow-esp-ota-release/2", "version": "5.6.3", "build_id": "B1", "commit": "c", "artifacts": []}
            for pid, data, sha in arts:
                m = {"artifact_type": "app", "version": "5.6.3", "build_id": "B1", "commit": "c", "size": len(data),
                     "sha256": sha or hashlib.sha256(data).hexdigest(), "app_elf_sha256": data[176:208].hex(),
                     "profile": {k: PROF[k] for k in v.CONSTRAINT_KEYS}}
                z.writestr(f"{pid}/app.bin", data); z.writestr(f"{pid}/manifest.json", json.dumps(m))
                rel["artifacts"].append({"profile_id": pid, "path": f"{pid}/app.bin", "manifest": f"{pid}/manifest.json"})
            z.writestr("release.json", json.dumps(rel))
        return path

    def test_good_release_passes(self):
        self.assertEqual(v.verify(self.pkg([("p-ok", app_image(), None)]), self.profiles), [])

    def test_sha_mismatch_fails(self):
        errs = v.verify(self.pkg([("p-ok", app_image(), "00" * 32)]), self.profiles)
        self.assertIn("[p-ok] SHA256_MISMATCH", errs)

    def test_unverified_profile_never_shipped(self):
        errs = v.verify(self.pkg([("p-ok", app_image(), None), ("p-linux", app_image("p-linux"), None)]), self.profiles)
        self.assertTrue(any("PROFILE_SET" in e for e in errs))
        self.assertIn("[p-linux] PROFILE_NOT_VERIFIED UNVERIFIED", errs)

    def test_bootloader_or_merged_image_rejected(self):
        errs = v.verify(self.pkg([("p-ok", app_image(magic=0x12345678), None)]), self.profiles)
        self.assertIn("[p-ok] NOT_APP_IMAGE", errs)
        errs = v.verify(self.pkg([("p-ok", app_image(first=0x00), None)]), self.profiles)
        self.assertIn("[p-ok] NOT_ESP32S3_IMAGE", errs)

    def test_identity_for_other_profile_rejected(self):
        errs = v.verify(self.pkg([("p-ok", app_image("p-linux"), None)]), self.profiles)
        self.assertIn("[p-ok] IDENTITY_MARKER_MISMATCH", errs)


if __name__ == "__main__":
    unittest.main()
