from pathlib import Path


SOURCE = Path(__file__).resolve().parents[1] / "esp" / "mesflow_app.cpp"


def test_three_wifi_slots_are_loaded_and_saved_without_touching_auth_namespaces():
    source = SOURCE.read_text()
    for key in ("wifi_ssid2", "wifi_pass2", "wifi_ssid3", "wifi_pass3"):
        assert f'getString("{key}"' in source
        assert f'putString("{key}"' in source
    save = source.split("bool saveDeviceConfig() {", 1)[1].split("static bool validServerBase", 1)[0]
    assert 'begin("mesflow_cfg"' in save
    assert 'begin("mesflow"' not in save
    assert 'putString("token"' not in save


def test_wifi_set_uses_existing_config_writer_and_never_clears_namespaces():
    source = SOURCE.read_text()
    command = source.split('if (cmd.startsWith("wifi-set "))', 1)[1].split('if (cmd == "restart"', 1)[0]
    assert "fallback_ssid" in command and "fallback2_ssid" in command
    assert "saveDeviceConfig()" in command
    assert ".clear()" not in command
    assert 'clearNamespace("mesflow")' not in command
    assert command.index("One or more network values are too long") < command.index("copyConfigValue(WIFI_SSID2")
    assert command.index("Server URL is not configured") < command.index("copyConfigValue(WIFI_SSID2")


def test_setup_cancel_resumes_with_configured_active_slot_without_rewriting_nvs():
    source = SOURCE.read_text()
    cancel = source.split("static void cancelSetupPortal() {", 1)[1].split("static void serviceSetupPortalInput()", 1)[0]
    assert "WiFi.begin(WIFI_SSID, WIFI_PASSWORD)" in cancel
    assert "saveDeviceConfig()" not in cancel


def test_operation_worker_guard_and_utf8_scanner_policy_remain_in_source():
    source = SOURCE.read_text()
    operation = source.split("if (isOperationQr(line.c_str()))", 1)[1]
    guard = operation.index('if (!rt.hasWorker || !rt.workerQr[0])')
    lookup = operation.index("lookupQr(line.c_str(), false)")
    assert guard < lookup
    assert '"WORKER_REQUIRED"' in operation[:lookup]
    assert '"mesflow_scanner_qr_policy.h"' in source
    assert "mfqr::isDataByte" in source
    assert "mfqr::isAsciiPrintable" in source
