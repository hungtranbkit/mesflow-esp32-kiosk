"""Regression: the LEGACY Deploy-Agent OTA path (single board, version bump).

Production Agent (deploy.mesflow.net, 2.24.37) still runs the legacy one-step
OTA store: POST /api/esp-ota/upload (.zip = exactly one app .bin + one
*.manifest.json, form kiosk_ids) -> create_job; the ESP polls
GET /api/esp-ota/check, downloads /api/esp-ota/file/<id>, reports
/api/esp-ota/event.  tests/fixtures/legacy_agent_c87edd09/ota_control.py is a
byte-for-byte copy of that store from Agent main c87edd09 (sha256 pinned
below) -- this test drives the real code, not a re-implementation.

Delta found 2026-11-12: release builds since 5.6.3 emit only the v2 package
(release.json + <profile>/app.bin + <profile>/manifest.json); the legacy Agent
rejects it (OTA_PACKAGE_CONTENT_INVALID), so "OTA does not work" although
the firmware side of the legacy contract is unchanged.  Fix:
tools/make_legacy_ota_zip.py converts the already-built release (same bytes,
same SHA-256) into the legacy 2-file package.
"""
from __future__ import annotations

import hashlib
import importlib.util
import io
import json
import re
import sys
import zipfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/fixtures/legacy_agent_c87edd09/ota_control.py"
FIXTURE_SHA = "be04ae249e4104603927be331a7faca6654b85a25c5e91ae0abd4105719645e8"
APP = (ROOT / "esp/mesflow_app.cpp").read_text(encoding="utf-8")
KIOSK = "b81f3fc440d0-test-uuid"
sys.path.insert(0, str(ROOT / "tools"))


def _fake_app(n=4096):
    head = bytearray(n)
    head[0] = 0xE9; head[12] = 0x09; head[13] = 0x00            # ESP32-S3 chip id
    head[32:36] = (0xABCD5432).to_bytes(4, "little")             # esp_app_desc magic
    return bytes(head)


def _v2_release(tmp, version="5.6.6", data=None, hw="ES3C28P", atype="app"):
    data = data or _fake_app()
    prof = "es3c28p-n16r8-m910-gm65uart1"
    man = {"schema": "mesflow-esp-ota-artifact/2", "artifact_type": atype, "version": version,
           "build_id": "20261112.0003.gtest", "profile": {"profile_id": prof, "hardware_model": hw},
           "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    p = tmp / f"esp-kiosk-{version}+test.ota.zip"
    with zipfile.ZipFile(p, "w") as z:
        z.writestr("release.json", json.dumps({"schema": "mesflow-esp-ota-release/2", "version": version,
                   "artifacts": [{"profile_id": prof, "path": f"{prof}/app.bin", "manifest": f"{prof}/manifest.json"}]}))
        z.writestr(f"{prof}/app.bin", data); z.writestr(f"{prof}/manifest.json", json.dumps(man))
    return p, data


@pytest.fixture()
def ota(tmp_path, monkeypatch):
    assert hashlib.sha256(FIXTURE.read_bytes()).hexdigest() == FIXTURE_SHA, "legacy fixture changed"
    monkeypatch.setenv("WORKSHOP_AGENT_HOME", str(tmp_path / "agent"))
    spec = importlib.util.spec_from_file_location("legacy_ota_control", FIXTURE)
    mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
    return mod


def _upload(ota, path):
    with open(path, "rb") as fh:
        return ota._save_firmware_package(fh, path.name)


def _legacy_zip(tmp, **kw):
    from make_legacy_ota_zip import convert
    rel, data = _v2_release(tmp, **kw)
    out = convert(rel, tmp / "legacy")
    assert len(out) == 1
    return out[0], data


def _online(ota, version="5.6.5", hw="ES3C28P", online=True):
    ota.sync_devices([{"kiosk_id": KIOSK, "kiosk_name": "40D0", "hardware_model": hw,
                       "firmware_version": version, "online": online}])


# --- the delta --------------------------------------------------------------
def test_v2_release_zip_is_rejected_by_legacy_agent(ota, tmp_path):
    rel, _ = _v2_release(tmp_path)
    with pytest.raises(ValueError, match="OTA_PACKAGE_CONTENT_INVALID"):
        _upload(ota, rel)


def test_converted_legacy_zip_is_accepted_same_bytes(ota, tmp_path):
    z, data = _legacy_zip(tmp_path)
    names = sorted(zipfile.ZipFile(z).namelist())
    assert len(names) == 2 and names[0].endswith(".bin") and names[1].endswith(".manifest.json")
    assert "merged" not in z.name.lower()
    fw = _upload(ota, z)
    assert fw["version"] == "5.6.6" and fw["hardware_model"] == "ES3C28P"
    assert fw["sha256"] == hashlib.sha256(data).hexdigest() and fw["size"] == len(data)


def test_converter_refuses_tampered_or_non_app(tmp_path):
    from make_legacy_ota_zip import convert
    rel, _ = _v2_release(tmp_path, atype="bootloader")
    with pytest.raises(SystemExit, match="NOT_APP"):
        convert(rel, tmp_path / "o1")
    bad = tmp_path / "bad.ota.zip"
    with zipfile.ZipFile(rel) as src, zipfile.ZipFile(bad, "w") as dst:
        for n in src.namelist():
            raw = src.read(n)
            if n.endswith("manifest.json"):
                m = json.loads(raw); m["artifact_type"] = "app"; m["sha256"] = "0" * 64; raw = json.dumps(m).encode()
            dst.writestr(n, raw)
    with pytest.raises(SystemExit, match="SHA256_MISMATCH"):
        convert(bad, tmp_path / "o2")


# --- the legacy flow end to end (Agent side, firmware request shapes) -------
def test_version_bump_upload_check_download_ack_success(ota, tmp_path):
    z, data = _legacy_zip(tmp_path)
    fw = _upload(ota, z); _online(ota)
    job = ota.create_job(fw, [KIOSK])
    assert [d["status"] for d in job["devices"]] == ["PENDING"]
    # firmware: /api/esp-ota/check?kiosk_id=<uuid>&current_version=FW_VERSION&hardware_model=HW_MODEL
    r = ota.check_update(KIOSK, "5.6.5", "ES3C28P")
    assert r["update_available"] is True
    for key in ("firmware_id", "version", "hardware_model", "size", "sha256"):   # what checkForOta() reads
        assert key in r
    assert r["hardware_model"] == "ES3C28P" and r["size"] == len(data)
    assert ota.target_for_download(KIOSK, r["firmware_id"])["sha256"] == hashlib.sha256(data).hexdigest()
    for i, ev in enumerate(["OTA_DOWNLOAD_START", "OTA_DOWNLOAD_COMPLETE", "OTA_VERIFY_OK", "OTA_REBOOTING"]):
        ota.record_event({"kiosk_id": KIOSK, "status": ev, "client_event_id": f"c{i}"})
    assert ota.get_job(job["id"])["devices"][0]["status"] == "REBOOTING"
    # after reboot: confirmPendingOtaBoot() -> boot ACK
    ota.record_event({"kiosk_id": KIOSK, "status": "OTA_BOOT_NEW_VERSION", "client_event_id": "b1"})
    ota.record_event({"kiosk_id": KIOSK, "status": "OTA_HEALTHCHECK_OK", "client_event_id": "b2"})
    done = ota.get_job(job["id"])
    assert done["status"] == "SUCCESS" and done["devices"][0]["status"] == "SUCCESS"
    assert ota.check_update(KIOSK, "5.6.6", "ES3C28P") == {"update_available": False}


def test_same_version_is_not_offered(ota, tmp_path):
    z, _ = _legacy_zip(tmp_path)
    fw = _upload(ota, z); _online(ota, version="5.6.6")
    ota.create_job(fw, [KIOSK])
    assert ota.check_update(KIOSK, "5.6.6", "ES3C28P") == {"update_available": False, "reason": "VERSION_NOT_NEWER"}
    assert ota.target_for_download(KIOSK, fw["id"]) is None   # never DOWNLOADING


def test_wrong_hardware_and_offline_refused_at_job_creation(ota, tmp_path):
    z, _ = _legacy_zip(tmp_path)
    fw = _upload(ota, z)
    _online(ota, hw="LINUX-KIOSK")
    with pytest.raises(ValueError, match="WRONG_HARDWARE"):
        ota.create_job(fw, [KIOSK])
    _online(ota, online=False)
    with pytest.raises(ValueError, match="DEVICE_OFFLINE"):
        ota.create_job(fw, [KIOSK])
    with pytest.raises(ValueError, match="DEVICE_NOT_FOUND"):
        ota.create_job(fw, ["other-board"])


def test_rollback_marks_failed_and_closes_download(ota, tmp_path):
    z, _ = _legacy_zip(tmp_path)
    fw = _upload(ota, z); _online(ota)
    job = ota.create_job(fw, [KIOSK])
    r = ota.check_update(KIOSK, "5.6.5", "ES3C28P")
    ota.record_event({"kiosk_id": KIOSK, "status": "OTA_DOWNLOAD_START"})
    ota.record_event({"kiosk_id": KIOSK, "status": "OTA_REBOOTING"})
    ota.record_event({"kiosk_id": KIOSK, "status": "OTA_ROLLBACK", "error_code": "OTA_BOOT_FAILED"})
    j = ota.get_job(job["id"])
    assert j["status"] == "FAILED" and j["devices"][0]["error"] == "OTA_BOOT_FAILED"
    assert ota.target_for_download(KIOSK, r["firmware_id"]) is None


def test_lost_boot_ack_reconciled_by_heartbeat_version(ota, tmp_path):
    z, _ = _legacy_zip(tmp_path)
    fw = _upload(ota, z); _online(ota)
    job = ota.create_job(fw, [KIOSK])
    ota.check_update(KIOSK, "5.6.5", "ES3C28P")
    ota.record_event({"kiosk_id": KIOSK, "status": "OTA_REBOOTING"})
    _online(ota, version="5.6.6")          # MESFlow inventory now reports the new version
    assert ota.get_job(job["id"])["status"] == "SUCCESS"


# --- firmware side of the same contract (source contract, no hardware) ------
def test_firmware_speaks_the_legacy_contract():
    assert re.search(r'#define HW_MODEL "ES3C28P"', APP)
    assert re.search(r'#define FW_VERSION "\d+\.\d+\.\d+"', APP)     # numeric: legacy _version_key
    assert "/api/esp-ota/check?kiosk_id=%s&current_version=%s&hardware_model=%s" in APP
    assert "DEVICE_UUID[0] ? DEVICE_UUID : DEVICE_ID, FW_VERSION, HW_MODEL" in APP
    # legacy check response has no artifact_type/hardware_profile/target_id: defaults must accept it
    assert 'response["artifact_type"] | "app"' in APP
    assert "HW_PROFILE[0] && profile[0] && strcmp(profile, HW_PROFILE)" in APP
    assert 'strcmp(model, HW_MODEL) != 0' in APP
    legacy_events = set(re.findall(r'"(OTA_[A-Z_]+)":"', FIXTURE.read_text()))
    for ev in ("OTA_DOWNLOAD_START", "OTA_DOWNLOAD_COMPLETE", "OTA_VERIFY_OK", "OTA_REBOOTING",
               "OTA_BOOT_NEW_VERSION", "OTA_HEALTHCHECK_OK", "OTA_ROLLBACK", "OTA_FAILED", "OTA_VERIFY_FAILED"):
        assert ev in legacy_events and f'"{ev}"' in APP, ev
    assert 'httpPostJson("/api/esp-ota/event"' in APP and "addAuthHeaders(http)" in APP
