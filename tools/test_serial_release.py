"""Exercise the real release shell/validator with a synthetic compiler edge."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def partition_table(serial_only):
    app_size = 0x300000 if serial_only else 0x330000
    entries = [(1, 2, 0x9000, 0x5000, "nvs"), (1, 0, 0xE000, 0x2000, "otadata"),
               (0, 0x10, 0x10000, app_size, "app0")]
    if serial_only:
        entries += [(1, 0x82, 0x310000, 0xE0000, "spiffs"), (1, 3, 0x3F0000, 0x10000, "coredump")]
    else:
        entries += [(0, 0x11, 0x340000, app_size, "app1"),
                    (1, 0x82, 0x670000, 0x180000, "spiffs"), (1, 3, 0x7F0000, 0x10000, "coredump")]
    raw = b"".join(struct.pack("<HBBII16sI", 0x50AA, typ, sub, off, size, label.encode(), 0)
                   for typ, sub, off, size, label in entries)
    raw += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(raw).digest()
    return raw.ljust(3072, b"\xff")


FAKE_COMPILER = r'''#!/usr/bin/env python3
import json, pathlib, re, sys
root=pathlib.Path.cwd()
args=sys.argv[1:]
out=pathlib.Path(args[args.index("--output-dir")+1])
work=pathlib.Path(args[args.index("--build-path")+1])
out.mkdir(parents=True,exist_ok=True);work.mkdir(parents=True,exist_ok=True)
header=(root/"esp/mesflow_build_profile.h").read_text()
pid=re.search(r'MESFLOW_HW_PROFILE "([^"]+)"',header).group(1)
build=re.search(r'MESFLOW_BUILD_ID "([^"]+)"',header).group(1)
app=bytearray(1024);app[0]=0xe9;app[12]=9
app[32:36]=(0xabcd5432).to_bytes(4,"little");app[176:208]=bytes(range(32))
identity=f"MESFLOW_FW_IDENTITY:version=5.6.9;build={build};profile={pid};hw=ES3C28P;end".encode()
app[300:300+len(identity)]=identity
(out/"esp.ino.bin").write_bytes(app)
(out/"esp.ino.partitions.bin").write_bytes((root/"fixtures"/(pid+".bin")).read_bytes())
record=root/"dist/compiled-profiles.txt"
with record.open("a") as f:f.write(pid+"\n")
capacity=3145728 if pid=="p-serial" else 3342336
print(f"Sketch uses 1024 bytes. Maximum is {capacity} bytes.")
'''


class SerialRelease(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for d in ("scripts", "tools", "profiles", "esp", "fixtures", "bin"):
            (self.root / d).mkdir()
        for name in ("scripts/build-ota-release.sh", "tools/make_legacy_ota_zip.py"):
            shutil.copy2(ROOT / name, self.root / name)
        (self.root / "esp/mesflow_app.cpp").write_text('#define FW_VERSION "5.6.9"\n#define FW_BUILD "20261127.0003"\n')
        (self.root / ".gitignore").write_text("dist/\nesp/mesflow_build_profile.h\n")
        for pid, serial_only in (("p-ota", False), ("p-serial", True)):
            table = partition_table(serial_only)
            (self.root / "fixtures" / (pid + ".bin")).write_bytes(table)
            p = dict(profile_id=pid, status="VERIFIED", hardware_model="ES3C28P", chip="esp32s3",
                     min_chip_revision=0, flash_size="16MB", psram="8MB-OPI", board="TEST",
                     scanner_interface="GM65-UART1-RX44-TXNC",
                     fqbn="esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=" +
                          ("huge_app" if serial_only else "default_8MB") + ",PSRAM=opi,CDCOnBoot=cdc",
                     partition_table_sha256=hashlib.sha256(table).hexdigest())
            if serial_only:
                p.update(delivery="serial-app-only", app_slot_size=3145728, app_offset=65536)
            else:
                p["ota_slot_size"] = 3342336
            (self.root / "profiles" / (pid + ".json")).write_text(json.dumps(p))
        compiler = self.root / "bin/arduino-cli"
        compiler.write_text(FAKE_COMPILER)
        compiler.chmod(0o755)
        self.git("init", "-q")
        self.commit()
        self.env = dict(os.environ, PATH=str(self.root / "bin") + os.pathsep + os.environ["PATH"],
                        MESFLOW_OTA_CA_FILE=str(self.root / "absent-ca.pem"))
        self.env.pop("MESFLOW_ALLOW_DIRTY", None)
        self.env.pop("MESFLOW_BUILD_TAG", None)

    def git(self, *args):
        return subprocess.run(["git", "-c", "user.name=FirmwareTests", "-c",
                               "user.email=tests@example.invalid", *args], cwd=self.root,
                              check=True, capture_output=True, text=True)

    def commit(self):
        self.git("add", ".")
        self.git("commit", "-qm", "test fixture")

    def build(self, *args):
        return subprocess.run(["bash", "scripts/build-ota-release.sh", *args], cwd=self.root,
                              env=self.env, capture_output=True, text=True, timeout=30)

    def test_serial_mode_builds_only_explicit_profile_and_never_packages_ota(self):
        result = self.build("--serial-profile", "p-serial")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / "dist/compiled-profiles.txt").read_text(), "p-serial\n")
        stage = self.root / "dist/serial-release/stage/p-serial"
        manifest = json.loads((stage / "manifest.json").read_text())
        self.assertEqual(manifest["schema"], "mesflow-esp-serial-artifact/1")
        self.assertEqual(manifest["delivery"], "serial-app-only")
        self.assertEqual(manifest["app_offset"], 0x10000)
        self.assertEqual(manifest["profile"]["app_slot_size"], 0x300000)
        self.assertNotIn("ota_slot_size", manifest["profile"])
        self.assertNotIn("rollback-enabled", manifest["bootloader_requirement"])
        self.assertEqual({p.name for p in stage.iterdir()}, {"app.bin", "manifest.json"})
        self.assertFalse(list((self.root / "dist").rglob("*.zip")))
        self.assertFalse((self.root / "dist/release").exists())

    def test_ota_mode_excludes_serial_profile_and_keeps_existing_package(self):
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / "dist/compiled-profiles.txt").read_text(), "p-ota\n")
        self.assertEqual(len(list((self.root / "dist/release").glob("*.ota.zip"))), 1)
        self.assertEqual(len(list((self.root / "dist/release/legacy").glob("*.zip"))), 1)
        self.assertFalse((self.root / "dist/release/stage/p-serial").exists())

    def test_serial_mode_rejects_an_ota_profile(self):
        result = self.build("--serial-profile", "p-ota")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SERIAL_PROFILE_REQUIRED", result.stdout + result.stderr)
        self.assertFalse((self.root / "dist/compiled-profiles.txt").exists())

    def test_serial_mode_rejects_partition_mismatch(self):
        path = self.root / "profiles/p-serial.json"
        prof = json.loads(path.read_text())
        prof["partition_table_sha256"] = "00" * 32
        path.write_text(json.dumps(prof))
        self.commit()
        result = self.build("--serial-profile", "p-serial")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("PARTITION_TABLE_MISMATCH", result.stdout + result.stderr)
        self.assertFalse(list((self.root / "dist").rglob("*.zip")))

    def test_real_huge_app_profile_matches_verified_physical_layout(self):
        prof = json.loads((ROOT / "profiles/es3c28p-n16r8-m910-hugeapp-serial.json").read_text())
        self.assertEqual(prof["status"], "VERIFIED")
        self.assertEqual(prof["delivery"], "serial-app-only")
        self.assertEqual(prof["partition_table_sha256"], hashlib.sha256(partition_table(True)).hexdigest())
        self.assertEqual(prof["partition_table_sha256"],
                         "aaae2888c5a6a348004b5b436f47abb25ae32e72d9003902955a998eda723edd")
        self.assertEqual(prof["app_offset"], 65536)
        self.assertEqual(prof["app_slot_size"], 3145728)
        self.assertEqual(prof["fqbn"],
                         "esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=huge_app,PSRAM=opi,CDCOnBoot=cdc")


def test_real_runtime_ota_capability_requires_distinct_inactive_slot():
    source = (ROOT / "esp/mesflow_app.cpp").read_text()
    signature = "static const esp_partition_t* inactiveOtaPartition()"
    start = source.index(signature)
    body = source.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    function = source[start:end]
    harness = r"""
#include <cassert>
#include <cstdint>
struct esp_partition_t { uint32_t address; uint32_t size; };
static const esp_partition_t* running = nullptr;
static const esp_partition_t* next = nullptr;
const esp_partition_t* esp_ota_get_running_partition() { return running; }
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return next; }
""" + function + r"""
int main() {
  esp_partition_t a{0x10000,0x300000}, alias{0x10000,0x300000}, b{0x340000,0x330000};
  assert(inactiveOtaPartition()==nullptr);
  running=&a; assert(inactiveOtaPartition()==nullptr);
  next=&a; assert(inactiveOtaPartition()==nullptr);
  next=&alias; assert(inactiveOtaPartition()==nullptr);
  next=&b; assert(inactiveOtaPartition()==&b);
  running=nullptr; assert(inactiveOtaPartition()==nullptr);
}
"""
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp)
        (p / "test.cpp").write_text(harness)
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(p / "test.cpp"),
                        "-o", str(p / "test")], check=True, capture_output=True, text=True)
        subprocess.run([str(p / "test")], check=True)
    assert source.count('request["ota_capable"] = inactiveOtaPartition() != nullptr;') == 2
    assert "const esp_partition_t* next = inactiveOtaPartition();" in source
