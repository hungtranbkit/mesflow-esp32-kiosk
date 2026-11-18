import base64
import json
import logging
import os

import pytest

from m910_usb_provisioner import (
    ConfigError, DeviceTarget, Provisioner, ProvisionerConfig, StateStore,
    ProcessLock, ProvisionerError, load_config, matching_ports,
)


MAC = "84:C7:BB:79:39:6C"
UUID = "fc0c33a9-2f76-424f-a256-217699b57712"
DEVICE_ID = "ESP32-KIOSK-396C"
SECRET = "never-log-this-wifi-password"


def _config(tmp_path, *, server="https://mesflow.example", serial_vid=0x303A,
            serial_pid=0x1001, devices=None):
    token = tmp_path / "service.token"
    token.write_text("fake-service-credential\n")
    token.chmod(0o600)
    password = tmp_path / "wifi.secret"
    password.write_text(SECRET + "\n")
    password.chmod(0o600)
    device = devices or [{
        "mac_address": MAC, "device_uuid": UUID, "station_code": "LASER-01",
        "wifi_ssid": "MESFlow-Test", "wifi_password_file": str(password),
    }]
    rows = "\n".join(
        "[[devices]]\n" + "\n".join(f'{k} = {json.dumps(v)}' for k, v in row.items())
        for row in device
    )
    path = tmp_path / "provisioner.toml"
    path.write_text(
        f'[server]\nbase_url = {json.dumps(server)}\nservice_token_file = {json.dumps(str(token))}\n'
        f'ca_file = "/etc/ssl/certs/ca-certificates.crt"\n'
        f'[serial]\nvid = {serial_vid}\npid = {serial_pid}\nbaud = 115200\n'
        f'[runtime]\nstate_db = {json.dumps(str(tmp_path / "state.sqlite3"))}\n'
        f'poll_seconds = 1\nmax_attempts_per_hour = 5\n{rows}\n'
    )
    path.chmod(0o600)
    return path, password


def test_manifest_requires_https_explicit_allowlist_and_unique_station(tmp_path):
    path, _ = _config(tmp_path, server="http://mesflow.example")
    with pytest.raises(ConfigError, match="(?i)https"):
        load_config(path, strict_permissions=True)

    duplicate = [
        {"mac_address": MAC, "device_uuid": UUID, "station_code": "LASER-01",
         "wifi_ssid": "x", "wifi_password_file": "/unused"},
        {"mac_address": "B8:1F:3F:C4:40:D0", "device_uuid": "b8e699f0-11fc-40c3-8143-8fd75e1fef5f",
         "station_code": "LASER-01", "wifi_ssid": "x", "wifi_password_file": "/unused"},
    ]
    path, _ = _config(tmp_path, devices=duplicate)
    with pytest.raises(ConfigError, match="station"):
        load_config(path, strict_permissions=True)


def test_manifest_rejects_symlink_even_when_target_is_protected(tmp_path):
    path, _ = _config(tmp_path)
    link = tmp_path / "manifest-link.toml"
    link.symlink_to(path)
    with pytest.raises(ConfigError, match="regular_file"):
        load_config(link, strict_permissions=True)


class FakeTransport:
    def __init__(self, info):
        self.info = dict(info)
        self.commands = []
        self.config = None

    def command(self, line, *, marker, timeout):
        self.commands.append(line.split(" ", 1)[0])
        if line == "usb-provision-hello":
            return "MESFLOW_USB_HELLO:" + json.dumps(self.info)
        if line == "maintenance confirm":
            return "[MAINT] USB maintenance confirm"
        if line.startswith("usb-provision-config "):
            payload = line.split(" ", 1)[1]
            self.config = json.loads(base64.urlsafe_b64decode(payload + "=" * (-len(payload) % 4)))
            self.info.update({"server_url": self.config["server_url"],
                              "station_code": self.config["station_code"],
                              "wifi_ssid": self.config["wifi_ssid"],
                              "device_id": self.config["device_id"],
                              "credential_present": False})
            return "USB_CONFIG_APPLIED"
        if line.startswith("usb-provision-proof "):
            return "MESFLOW_USB_PROOF:" + json.dumps({
                "device_uuid": UUID, "challenge": line.rsplit(" ", 1)[1],
                "mac_address": MAC, "device_id": DEVICE_ID,
                "station_code": "LASER-01", "firmware_version": "5.6.6",
                "proof": "a" * 64,
            })
        if line == f"usb-provision-commit {DEVICE_ID}":
            self.info["device_id"] = DEVICE_ID
            return f"[USB-PROVISION] DEVICE_ID_SAVED {DEVICE_ID}"
        if line == "maintenance exit":
            return "[USB-PROVISION] EXITING"
        raise AssertionError("unexpected command")


class FakeAPI:
    def __init__(self):
        self.state = {"status": "NOT_FOUND"}
        self.complete_calls = 0

    def status(self, device_uuid):
        assert device_uuid == UUID
        return dict(self.state)

    def challenge(self, device_uuid):
        assert device_uuid == UUID
        return {"challenge": "c" * 64}

    def complete(self, proof):
        self.complete_calls += 1
        assert proof["proof"] == "a" * 64
        self.state = {"status": "PENDING", "provisionable": True,
                      "enrollment_window_open": True, "device_id": DEVICE_ID,
                      "mac_address": MAC, "station_code": "LASER-01"}
        return {"approved": True, "device_id": DEVICE_ID}


def _target(tmp_path):
    path, _ = _config(tmp_path)
    return load_config(path, strict_permissions=True).devices[0]


def _hello(**overrides):
    return {"device_uuid": UUID, "mac_address": MAC,
            "device_id": "ESP32-KIOSK-001", "station_code": "LASER-01",
            "server_url": "", "wifi_ssid": "", "bound": False,
            "credential_present": False, "idle": True,
            "pending_transaction": False, "queued_events": False,
            "ota_in_progress": False, **overrides}


def test_new_board_config_then_enrolls_and_retries_idempotently_offline(tmp_path, caplog):
    target = _target(tmp_path)
    config = ProvisionerConfig(base_url="https://mesflow.example", token_file="unused",
                               ca_file="unused", state_db=str(tmp_path / "state.sqlite3"),
                               vid=0x303A, pid=0x1001, baud=115200,
                               poll_seconds=1, max_attempts_per_hour=5, devices=(target,))
    api = FakeAPI()
    transport = FakeTransport(_hello())
    store = StateStore(config.state_db)
    p = Provisioner(config, api=api, state=store)

    assert p.process(target, transport) == "configured-awaiting-connect"
    assert transport.config["server_url"] == config.base_url
    assert transport.config["station_code"] == "LASER-01"
    assert transport.config["wifi_password"] == SECRET
    assert api.complete_calls == 0
    assert SECRET not in caplog.text
    assert SECRET not in json.dumps(store.snapshot())

    api.state = {"status": "PENDING", "provisionable": True,
                 "enrollment_window_open": False, "station_code": "LASER-01"}
    transport.info.update({"server_url": config.base_url,
                           "wifi_ssid": "MESFlow-Test"})
    assert p.process(target, transport) == "awaiting-active"
    assert api.complete_calls == 1


def test_factory_device_id_still_gets_migrated_when_other_config_already_matches(tmp_path):
    target = _target(tmp_path)
    config = ProvisionerConfig("https://mesflow.example", "unused", "unused",
                               str(tmp_path / "state.sqlite3"), 0x303A, 0x1001,
                               115200, 1, 5, (target,))
    hello = _hello(server_url=config.base_url, station_code=target.station_code,
                   wifi_ssid=target.wifi_ssid)
    transport = FakeTransport(hello)
    api = FakeAPI()
    p = Provisioner(config, api=api, state=StateStore(config.state_db))
    assert p.process(target, transport) == "configured-awaiting-connect"
    assert transport.config["device_id"] == target.device_id
    assert "usb-provision-config" in transport.commands


def test_pending_identity_can_be_safely_configured_before_usb_proof(tmp_path, caplog):
    target = _target(tmp_path)
    config = ProvisionerConfig("https://mesflow.example", "unused", "unused",
                               str(tmp_path / "state.sqlite3"), 0x303A, 0x1001,
                               115200, 1, 5, (target,))
    api = FakeAPI()
    api.state = {"status": "PENDING", "provisionable": True,
                 "enrollment_window_open": False, "station_code": target.station_code}
    transport = FakeTransport(_hello())
    store = StateStore(config.state_db)
    p = Provisioner(config, api=api, state=store)
    assert p.process(target, transport) == "configured-awaiting-connect"
    assert transport.config["device_id"] == target.device_id
    assert api.complete_calls == 0
    assert p.process(target, transport) == "awaiting-active"
    assert api.complete_calls == 1
    assert "usb-provision-proof" in transport.commands  # fake stores verbs, never payloads
    assert "usb-provision-config" in transport.commands
    assert "fake-service-credential" not in caplog.text
    assert "a" * 64 not in caplog.text

    api.state = {"status": "ACTIVE", "provisionable": False,
                 "device_id": DEVICE_ID, "mac_address": MAC,
                 "station_code": "LASER-01"}
    assert p.process(target, transport) == "active"
    assert api.complete_calls == 1


def test_unknown_or_mismatched_usb_device_is_never_configured(tmp_path):
    target = _target(tmp_path)
    config = ProvisionerConfig("https://mesflow.example", "unused", "unused",
                               str(tmp_path / "state.sqlite3"), 0x303A, 0x1001,
                               115200, 1, 5, (target,))
    transport = FakeTransport(_hello(mac_address="02:00:00:00:00:01"))
    p = Provisioner(config, api=FakeAPI(), state=StateStore(config.state_db))
    assert p.process(target, transport) == "identity-mismatch"
    assert "usb-provision-config" not in transport.commands
    assert transport.commands == ["usb-provision-hello"]


@pytest.mark.parametrize("unsafe", [
    {"idle": False}, {"pending_transaction": True},
    {"queued_events": True}, {"ota_in_progress": True},
])
def test_busy_board_never_receives_wifi_or_config_commands(tmp_path, unsafe):
    target = _target(tmp_path)
    config = ProvisionerConfig("https://mesflow.example", "unused", "unused",
                               str(tmp_path / "state.sqlite3"), 0x303A, 0x1001,
                               115200, 1, 5, (target,))
    transport = FakeTransport(_hello(**unsafe))
    p = Provisioner(config, api=FakeAPI(), state=StateStore(config.state_db))
    assert p.process(target, transport) == "ineligible"
    assert transport.commands == ["usb-provision-hello"]
    assert transport.config is None


@pytest.mark.parametrize("status", ["DISABLED", "ACTIVE"])
def test_existing_identity_is_never_reconfigured_or_reactivated(tmp_path, status):
    target = _target(tmp_path)
    config = ProvisionerConfig("https://mesflow.example", "unused", "unused",
                               str(tmp_path / "state.sqlite3"), 0x303A, 0x1001,
                               115200, 1, 5, (target,))
    api = FakeAPI()
    api.state = {"status": status, "provisionable": False,
                 "device_id": DEVICE_ID if status == "ACTIVE" else "",
                 "mac_address": MAC, "station_code": "LASER-01"}
    transport = FakeTransport(_hello())
    p = Provisioner(config, api=api, state=StateStore(config.state_db))
    assert p.process(target, transport) == ("active" if status == "ACTIVE" else "ineligible")
    assert "usb-provision-config" not in transport.commands
    assert api.complete_calls == 0


def test_process_lock_excludes_duplicate_workers(tmp_path):
    path = tmp_path / "run" / "provisioner.lock"
    with ProcessLock(path):
        with pytest.raises(ProvisionerError, match="another_provisioner"):
            with ProcessLock(path):
                pass


def test_usb_enumerator_matches_only_explicit_vid_pid():
    class Port:
        def __init__(self, device, vid, pid):
            self.device, self.vid, self.pid = device, vid, pid
    ports = [Port("/dev/tty1", 0x303A, 0x1001),
             Port("/dev/tty2", 0x303A, 0x1002),
             Port("/dev/tty3", None, None)]
    assert [p.device for p in matching_ports(ports, 0x303A, 0x1001)] == ["/dev/tty1"]
