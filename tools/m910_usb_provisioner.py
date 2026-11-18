#!/usr/bin/env python3
"""M910 trusted USB ESP provisioner. Safe to import/test without serial devices.

The service only acts on exact MAC+UUID entries in an explicit root-owned
manifest. It never discovers/creates identities or chooses a station itself.
"""
from __future__ import annotations

import base64
import fcntl
import hashlib
import json
import logging
import os
import re
import sqlite3
import ssl
import stat
import time
import tomllib
import urllib.error
import urllib.parse
import urllib.request
import uuid as uuidlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


MAC_RE = re.compile(r"^(?:[0-9A-F]{2}:){5}[0-9A-F]{2}$")
ID_RE = re.compile(r"^ESP32-KIOSK-[0-9A-F]{4}$")
PROOF_RE = re.compile(r"^[0-9a-fA-F]{64}$")
FACTORY_ID = "ESP32-KIOSK-001"
DEFAULT_LOGGER = logging.getLogger("m910-kiosk-provisioner")


class ConfigError(ValueError):
    pass


class ProvisionerError(RuntimeError):
    def __init__(self, code: str):
        super().__init__(code)
        self.code = code


def _mac(value: str) -> str:
    result = str(value or "").strip().upper().replace("-", ":")
    if not MAC_RE.fullmatch(result):
        raise ConfigError("invalid_mac")
    return result


def _uuid(value: str) -> str:
    try:
        result = str(uuidlib.UUID(str(value)))
    except (ValueError, AttributeError, TypeError) as exc:
        raise ConfigError("invalid_uuid") from exc
    if result != str(value).lower():
        raise ConfigError("uuid_must_be_canonical")
    return result


def device_id_for_mac(mac_address: str) -> str:
    mac = _mac(mac_address)
    return "ESP32-KIOSK-" + mac.replace(":", "")[-4:]


def read_secret_file(path: str | Path, *, strict_permissions: bool = True,
                     max_bytes: int = 4096) -> str:
    """Read one secret from a regular, non-symlink file; never log its value."""
    p = Path(path)
    try:
        before = p.lstat()
        if not stat.S_ISREG(before.st_mode) or stat.S_ISLNK(before.st_mode):
            raise ProvisionerError("secret_file_not_regular")
        if strict_permissions and (before.st_mode & 0o077):
            raise ProvisionerError("secret_file_permissions")
        fd = os.open(p, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
        try:
            opened = os.fstat(fd)
            if not stat.S_ISREG(opened.st_mode) or opened.st_ino != before.st_ino:
                raise ProvisionerError("secret_file_changed")
            raw = os.read(fd, max_bytes + 1)
        finally:
            os.close(fd)
    except ProvisionerError:
        raise
    except OSError as exc:
        raise ProvisionerError("secret_file_unavailable") from exc
    if len(raw) > max_bytes:
        raise ProvisionerError("secret_file_too_large")
    value = raw.decode("utf-8").rstrip("\r\n")
    if not value or "\x00" in value:
        raise ProvisionerError("secret_file_empty_or_invalid")
    return value


@dataclass(frozen=True)
class DeviceTarget:
    mac_address: str
    device_uuid: str
    station_code: str
    wifi_ssid: str
    wifi_password_file: str

    @property
    def device_id(self) -> str:
        return device_id_for_mac(self.mac_address)


@dataclass(frozen=True)
class ProvisionerConfig:
    base_url: str
    token_file: str
    ca_file: str
    state_db: str
    vid: int
    pid: int
    baud: int
    poll_seconds: int
    max_attempts_per_hour: int
    devices: tuple[DeviceTarget, ...]


def load_config(path: str | Path, *, strict_permissions: bool = True) -> ProvisionerConfig:
    p = Path(path)
    try:
        st = p.lstat()
        if not stat.S_ISREG(st.st_mode) or stat.S_ISLNK(st.st_mode):
            raise ConfigError("config_must_be_regular_file")
        if strict_permissions and (st.st_mode & 0o077):
            raise ConfigError("config_permissions_must_be_0600")
        with p.open("rb") as stream:
            data = tomllib.load(stream)
    except ConfigError:
        raise
    except (OSError, tomllib.TOMLDecodeError) as exc:
        raise ConfigError("config_unreadable_or_invalid") from exc

    server = data.get("server", {})
    base_url = str(server.get("base_url", "")).strip().rstrip("/")
    parsed = urllib.parse.urlsplit(base_url)
    if parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password or parsed.query or parsed.fragment:
        raise ConfigError("server_base_url_must_be_https")
    if parsed.path not in ("", "/"):
        raise ConfigError("server_base_url_must_be_origin")
    if len(base_url.encode("utf-8")) > 190:
        raise ConfigError("server_base_url_too_long_for_usb_protocol")
    token_file = str(server.get("service_token_file", "")).strip()
    ca_file = str(server.get("ca_file", "")).strip()
    if not token_file or not ca_file or not Path(ca_file).is_file():
        raise ConfigError("server_credentials_or_ca_missing")

    serial = data.get("serial", {})
    try:
        vid, pid = int(serial["vid"]), int(serial["pid"])
        baud = int(serial.get("baud", 115200))
    except (KeyError, TypeError, ValueError) as exc:
        raise ConfigError("explicit_usb_vid_pid_required") from exc
    if not (1 <= vid <= 0xFFFF and 1 <= pid <= 0xFFFF and baud == 115200):
        raise ConfigError("unsupported_usb_match_or_baud")

    runtime = data.get("runtime", {})
    state_db = str(runtime.get("state_db", "")).strip()
    poll_seconds = int(runtime.get("poll_seconds", 2))
    max_attempts = int(runtime.get("max_attempts_per_hour", 5))
    if not state_db or not (1 <= poll_seconds <= 30) or not (1 <= max_attempts <= 10):
        raise ConfigError("invalid_runtime_limits")

    rows = data.get("devices", [])
    if not rows:
        raise ConfigError("explicit_device_allowlist_required")
    devices: list[DeviceTarget] = []
    macs: set[str] = set()
    uuids: set[str] = set()
    stations: set[str] = set()
    ids: set[str] = set()
    for row in rows:
        mac = _mac(row.get("mac_address", ""))
        device_uuid = _uuid(row.get("device_uuid", ""))
        station = str(row.get("station_code", "")).strip().upper()
        ssid = str(row.get("wifi_ssid", "")).strip()
        password_file = str(row.get("wifi_password_file", "")).strip()
        if not station or len(station) > 39 or not ssid or len(ssid.encode()) > 32 or not password_file:
            raise ConfigError("explicit_station_and_wifi_reference_required")
        device_id = device_id_for_mac(mac)
        if mac in macs or device_uuid in uuids or station in stations or device_id in ids:
            raise ConfigError("duplicate_mac_uuid_station_or_device_id")
        macs.add(mac); uuids.add(device_uuid); stations.add(station); ids.add(device_id)
        devices.append(DeviceTarget(mac, device_uuid, station, ssid, password_file))

    return ProvisionerConfig(base_url, token_file, ca_file, state_db, vid, pid,
                             baud, poll_seconds, max_attempts, tuple(devices))


class StateStore:
    """Durable idempotency/retry metadata. Stores no tokens, Wi-Fi, proof or nonce."""
    def __init__(self, path: str | Path):
        self.path = str(path)
        Path(self.path).parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        self._db = sqlite3.connect(self.path, timeout=5, isolation_level=None)
        os.chmod(self.path, 0o600)
        self._db.execute("PRAGMA journal_mode=WAL")
        self._db.execute("""CREATE TABLE IF NOT EXISTS provision_state(
            device_uuid TEXT PRIMARY KEY, mac_address TEXT NOT NULL,
            phase TEXT NOT NULL, failures INTEGER NOT NULL DEFAULT 0,
            window_start REAL NOT NULL DEFAULT 0, last_code TEXT NOT NULL DEFAULT '',
            updated_at REAL NOT NULL)""")

    def close(self) -> None:
        self._db.close()

    def snapshot(self) -> list[dict[str, Any]]:
        rows = self._db.execute("SELECT device_uuid,mac_address,phase,failures,last_code FROM provision_state ORDER BY device_uuid").fetchall()
        return [dict(zip(("device_uuid", "mac_address", "phase", "failures", "last_code"), row)) for row in rows]

    def phase(self, target: DeviceTarget) -> str:
        row = self._db.execute("SELECT phase FROM provision_state WHERE device_uuid=? AND mac_address=?",
                                (target.device_uuid, target.mac_address)).fetchone()
        return row[0] if row else "new"

    def record(self, target: DeviceTarget, phase: str, code: str = "") -> None:
        self._db.execute("""INSERT INTO provision_state(device_uuid,mac_address,phase,last_code,updated_at)
            VALUES(?,?,?,?,?) ON CONFLICT(device_uuid) DO UPDATE SET mac_address=excluded.mac_address,
            phase=excluded.phase,last_code=excluded.last_code,updated_at=excluded.updated_at""",
            (target.device_uuid, target.mac_address, phase, code[:48], time.time()))

    def allow_mutation(self, target: DeviceTarget, limit: int) -> bool:
        now = time.time()
        with self._db:
            row = self._db.execute("SELECT failures,window_start FROM provision_state WHERE device_uuid=?",
                                   (target.device_uuid,)).fetchone()
            failures, start = row if row else (0, now)
            if now - start >= 3600:
                failures, start = 0, now
            if failures >= limit:
                return False
            self._db.execute("""INSERT INTO provision_state(device_uuid,mac_address,phase,failures,window_start,updated_at)
                VALUES(?,?, 'attempting', ?, ?, ?) ON CONFLICT(device_uuid) DO UPDATE SET
                mac_address=excluded.mac_address,failures=excluded.failures,window_start=excluded.window_start,
                phase='attempting',updated_at=excluded.updated_at""",
                (target.device_uuid, target.mac_address, failures + 1, start, now))
        return True


class ProvisionerAPI:
    """HTTPS-only client for the narrow M910 service-principal endpoints."""
    def __init__(self, config: ProvisionerConfig, *, token: str | None = None):
        self.config = config
        self._token = token if token is not None else read_secret_file(config.token_file)
        self._tls = ssl.create_default_context(cafile=config.ca_file)
        self._opener = urllib.request.build_opener(_NoRedirect(), urllib.request.HTTPSHandler(context=self._tls))

    def _request(self, method: str, path: str, body: dict | None = None) -> dict:
        payload = b"" if body is None else json.dumps(body, separators=(",", ":")).encode()
        request = urllib.request.Request(self.config.base_url + path, data=payload or None,
            method=method, headers={"Accept": "application/json", "Content-Type": "application/json",
                                     "X-MESFlow-Provisioner": self._token})
        try:
            with self._opener.open(request, timeout=8) as response:
                result = json.loads(response.read(8193))
        except (urllib.error.URLError, TimeoutError, OSError, ValueError) as exc:
            raise ProvisionerError("api_unavailable_or_invalid") from exc
        if not isinstance(result, dict):
            raise ProvisionerError("api_invalid_response")
        if result.get("ok") is False:
            raise ProvisionerError(str(result.get("code") or "api_rejected")[:48])
        return result

    def status(self, device_uuid: str) -> dict:
        query = urllib.parse.urlencode({"device_uuid": device_uuid})
        return self._request("GET", "/api/kiosk-provisioner/usb/status?" + query)

    def challenge(self, device_uuid: str) -> dict:
        return self._request("POST", "/api/kiosk-provisioner/usb/challenge", {"device_uuid": device_uuid})

    def complete(self, proof: dict) -> dict:
        return self._request("POST", "/api/kiosk-provisioner/usb/complete", proof)


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise urllib.error.HTTPError(req.full_url, code, "redirect_refused", headers, fp)


class Provisioner:
    def __init__(self, config: ProvisionerConfig, *, api, state: StateStore,
                 logger: logging.Logger = DEFAULT_LOGGER):
        self.config = config
        self.api = api
        self.state = state
        self.log = logger

    @staticmethod
    def _json_line(line: str, prefix: str) -> dict:
        if not line.startswith(prefix):
            raise ProvisionerError("device_protocol_mismatch")
        try:
            value = json.loads(line[len(prefix):])
        except (ValueError, TypeError) as exc:
            raise ProvisionerError("device_json_invalid") from exc
        if not isinstance(value, dict):
            raise ProvisionerError("device_json_invalid")
        return value

    def _save(self, target: DeviceTarget, phase: str, code: str = "") -> str:
        self.state.record(target, phase, code)
        self.log.info("kiosk provisioning phase=%s uuid=%s code=%s",
                      phase, target.device_uuid[:8], code[:48])
        return phase

    def process(self, target: DeviceTarget, transport) -> str:
        """Process one already-open, OS-exclusive serial transport."""
        try:
            hello = self._json_line(transport.command("usb-provision-hello",
                marker="MESFLOW_USB_HELLO:", timeout=4), "MESFLOW_USB_HELLO:")
            if _mac(hello.get("mac_address", "")) != target.mac_address or _uuid(hello.get("device_uuid", "")) != target.device_uuid:
                return self._save(target, "identity-mismatch", "allowlist_mismatch")

            server = self.api.status(target.device_uuid)
            status = str(server.get("status", "")).upper()
            if status == "ACTIVE":
                if (server.get("device_id") != target.device_id or
                        str(server.get("station_code", "")).upper() != target.station_code):
                    return self._save(target, "active-mismatch", "active_binding_mismatch")
                return self._save(target, "active")
            if status in {"DISABLED", "REVOKED", "SUSPENDED"}:
                return self._save(target, "ineligible", "server_status_denied")
            if status == "NOT_FOUND":
                if hello.get("bound") or hello.get("credential_present"):
                    return self._save(target, "ineligible", "device_has_existing_credential")
                if not self._local_safe(hello):
                    return self._save(target, "ineligible", "device_not_idle_or_has_pending_work")
                current_id = str(hello.get("device_id", ""))
                if current_id not in (FACTORY_ID, target.device_id):
                    return self._save(target, "identity-mismatch", "unexpected_device_id")
                if not self._config_matches(hello, target):
                    if not self.state.allow_mutation(target, self.config.max_attempts_per_hour):
                        return self._save(target, "retry-limited", "attempt_budget_exhausted")
                    self._configure(target, transport)
                    return self._save(target, "configured-awaiting-connect")
                return self._save(target, "configured-awaiting-connect")

            if status != "PENDING" or not bool(server.get("provisionable")):
                return self._save(target, "ineligible", "identity_not_fresh_pending")
            if (str(server.get("station_code", "")).upper() != target.station_code or
                    hello.get("bound") or hello.get("credential_present") or not self._local_safe(hello) or
                    hello.get("device_id") not in (FACTORY_ID, target.device_id)):
                return self._save(target, "ineligible", "pending_configuration_mismatch")
            if not self._config_matches(hello, target):
                if server.get("enrollment_window_open"):
                    return self._save(target, "ineligible", "approved_identity_configuration_mismatch")
                if not self.state.allow_mutation(target, self.config.max_attempts_per_hour):
                    return self._save(target, "retry-limited", "attempt_budget_exhausted")
                self._configure(target, transport)
                return self._save(target, "configured-awaiting-connect")
            if server.get("enrollment_window_open"):
                approved_id = str(server.get("device_id", ""))
                if approved_id != target.device_id:
                    return self._save(target, "ineligible", "approved_device_id_mismatch")
                self._enter_maintenance(transport)
                transport.command("usb-provision-commit " + target.device_id,
                    marker="[USB-PROVISION] DEVICE_ID_SAVED " + target.device_id, timeout=5)
                transport.command("maintenance exit", marker="[MAINT] EXIT", timeout=3)
                return self._save(target, "awaiting-active")

            if not self.state.allow_mutation(target, self.config.max_attempts_per_hour):
                return self._save(target, "retry-limited", "attempt_budget_exhausted")
            challenge = str(self.api.challenge(target.device_uuid).get("challenge", ""))
            if not PROOF_RE.fullmatch(challenge):
                raise ProvisionerError("challenge_invalid")
            self._enter_maintenance(transport)
            line = transport.command("usb-provision-proof " + challenge,
                marker="MESFLOW_USB_PROOF:", timeout=8)
            proof = self._json_line(line, "MESFLOW_USB_PROOF:")
            if (_uuid(proof.get("device_uuid", "")) != target.device_uuid or
                    _mac(proof.get("mac_address", "")) != target.mac_address or
                    proof.get("device_id") != target.device_id or
                    str(proof.get("station_code", "")).upper() != target.station_code or
                    not PROOF_RE.fullmatch(str(proof.get("proof", "")))):
                raise ProvisionerError("attestation_mismatch")
            proof["challenge"] = challenge
            result = self.api.complete(proof)
            if result.get("approved") is not True or result.get("device_id") != target.device_id:
                raise ProvisionerError("server_approval_invalid")
            transport.command("usb-provision-commit " + target.device_id,
                marker="[USB-PROVISION] DEVICE_ID_SAVED " + target.device_id, timeout=5)
            transport.command("maintenance exit", marker="[MAINT] EXIT", timeout=3)
            return self._save(target, "awaiting-active")
        except ProvisionerError as exc:
            return self._save(target, "blocked", exc.code)
        except Exception:
            # Do not stringify arbitrary transport/network exceptions: drivers
            # sometimes include raw line buffers in their exception messages.
            return self._save(target, "blocked", "unexpected_error_redacted")

    def _enter_maintenance(self, transport) -> None:
        transport.command("maintenance confirm",
            marker="[MAINT] USB maintenance confirm", timeout=8)

    def _configure(self, target: DeviceTarget, transport) -> None:
        self._enter_maintenance(transport)
        password = read_secret_file(target.wifi_password_file)
        config_doc = {"expected_uuid": target.device_uuid,
            "mac_address": target.mac_address, "device_id": target.device_id,
            "server_url": self.config.base_url, "station_code": target.station_code,
            "wifi_ssid": target.wifi_ssid, "wifi_password": password}
        encoded = base64.urlsafe_b64encode(json.dumps(config_doc, separators=(",", ":")).encode()).decode().rstrip("=")
        transport.command("usb-provision-config " + encoded,
            marker="USB_CONFIG_APPLIED", timeout=10)

    def _config_matches(self, hello: dict, target: DeviceTarget) -> bool:
        return (hello.get("server_url", "").rstrip("/") == self.config.base_url and
                str(hello.get("station_code", "")).upper() == target.station_code and
                hello.get("wifi_ssid") == target.wifi_ssid and
                hello.get("device_id") == target.device_id)

    @staticmethod
    def _local_safe(hello: dict) -> bool:
        return (hello.get("idle") is True and hello.get("pending_transaction") is False and
                hello.get("queued_events") is False and hello.get("ota_in_progress") is False)


def matching_ports(ports: Iterable[Any], vid: int, pid: int) -> list[Any]:
    return sorted((p for p in ports if getattr(p, "vid", None) == vid and
                   getattr(p, "pid", None) == pid and getattr(p, "device", None)),
                  key=lambda p: str(p.device))


class ProcessLock:
    """Single daemon instance lock; OS serial open also uses TIOCEXCL."""
    def __init__(self, path: str | Path):
        self.path = str(path)
        self.fd: int | None = None

    def __enter__(self):
        Path(self.path).parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        self.fd = os.open(self.path, os.O_CREAT | os.O_RDWR, 0o600)
        try:
            fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            os.close(self.fd); self.fd = None
            raise ProvisionerError("another_provisioner_is_running") from exc
        return self

    def __exit__(self, *_):
        if self.fd is not None:
            fcntl.flock(self.fd, fcntl.LOCK_UN)
            os.close(self.fd)
            self.fd = None


class SerialTransport:
    """Line protocol with bounded reads; no received line is logged."""
    def __init__(self, port, baud: int = 115200):
        try:
            import serial
        except ImportError as exc:
            raise ProvisionerError("pyserial_not_installed") from exc
        self.serial = serial.Serial(port, baudrate=baud, timeout=0.2,
                                    write_timeout=1, exclusive=True)

    def close(self):
        self.serial.close()

    def command(self, line: str, *, marker: str, timeout: float) -> str:
        self.serial.reset_input_buffer()
        self.serial.write((line + "\n").encode("ascii"))
        self.serial.flush()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            raw = self.serial.readline(2048)
            if not raw:
                continue
            text = raw.decode("utf-8", errors="replace").strip()
            if text.startswith(marker):
                return text
        raise ProvisionerError("serial_timeout")


def main(argv: list[str] | None = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="/etc/mesflow/kiosk-provisioner/provisioner.toml")
    args = parser.parse_args(argv)
    config = load_config(args.config, strict_permissions=True)
    lock_path = str(Path(config.state_db).with_suffix(".lock"))
    state = None
    try:
        with ProcessLock(lock_path):
            try:
                import serial.tools.list_ports
            except ImportError as exc:
                raise ProvisionerError("pyserial_not_installed") from exc
            api = ProvisionerAPI(config)
            state = StateStore(config.state_db)
            provisioner = Provisioner(config, api=api, state=state)
            while True:
                ports = matching_ports(serial.tools.list_ports.comports(), config.vid, config.pid)
                for port in ports:
                    try:
                        transport = SerialTransport(port.device, config.baud)
                        try:
                            line = transport.command("usb-provision-hello", marker="MESFLOW_USB_HELLO:", timeout=4)
                            hello = provisioner._json_line(line, "MESFLOW_USB_HELLO:")
                            mac = _mac(hello.get("mac_address", ""))
                            target = next((d for d in config.devices if d.mac_address == mac and
                                           d.device_uuid == _uuid(hello.get("device_uuid", ""))), None)
                            if target is not None:
                                provisioner.process(target, transport)
                        finally:
                            transport.close()
                    except ProvisionerError as exc:
                        DEFAULT_LOGGER.warning("USB candidate skipped code=%s", exc.code)
                    except Exception:
                        DEFAULT_LOGGER.warning("USB candidate skipped code=serial_or_identity_error")
                time.sleep(config.poll_seconds)
    except ProvisionerError as exc:
        DEFAULT_LOGGER.error("provisioner stopped code=%s", exc.code)
        return 2
    finally:
        if state is not None:
            state.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
