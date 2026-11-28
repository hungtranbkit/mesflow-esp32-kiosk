"""Source contracts for duplicate QR suppression and USB-only scanner probing."""
from pathlib import Path
from test_enrollment_display import function

ROOT = Path(__file__).parents[1]
SOURCE = (ROOT / "esp/mesflow_app.cpp").read_text()


def body(signature: str) -> str:
    start = SOURCE.index(signature)
    i = SOURCE.index("{", start)
    depth = 0
    for j in range(i, len(SOURCE)):
        depth += {"{": 1, "}": -1}.get(SOURCE[j], 0)
        if depth == 0:
            return SOURCE[i:j + 1]
    raise AssertionError(signature)


def test_probe_is_gated_usb_maintenance_and_verified_profile_only():
    console = body("static bool handleConsoleCommand(String line) {")
    usb = body("static void handleUsbConsoleLine(String line) {")
    probe = body("static void runScannerProbe() {")
    assert 'cmd == "scanner-probe"' in console and "runScannerProbe()" in console
    assert "mfmt::allowedDuringMaintenance" in usb
    assert "mfmt::mayRunScannerProbe(in)" in probe
    assert probe.index("if (!mfmt::mayRunScannerProbe(in))") < probe.index("pinMode(")
    assert "scannerProbeProfileSupported()" in probe
    assert '"es3c28p-n16r8-m910-gm65uart1"' in SOURCE
    assert '"es3c28p-n16r8-m910-hugeapp-serial"' in SOURCE


def test_probe_only_samples_and_drains_without_state_or_config_mutation():
    probe = body("static void runScannerProbe() {")
    for required in ("ScannerSerial.available()", "ScannerSerial.read()", "digitalRead(SCANNER_RX_PIN)",
                     "esp_task_wdt_reset()", "delay(0)", "scannerFrameLength = 0"):
        assert required in probe
    for forbidden in ("Preferences", "saveDeviceConfig", "WiFi.", "ESP.restart", "reattachScanner",
                      "scannerBaud =", "Update.", "DEVICE_UUID", "kioskToken"):
        assert forbidden not in probe, forbidden


def test_duplicate_suppression_precedes_payload_logging_and_dispatch():
    dispatch = body("void dispatchScannerFrame(const char* reason) {")
    guard = dispatch.index("mfqr::isRecentRepeat")
    assert guard < dispatch.index('Serial.print("DATA : ")')
    assert guard < dispatch.index("handleSerialLine(scannerText)")
    assert "duplicate valid QR suppressed; length=" in dispatch
    reader = body("void readScannerCommands() {")
    assert "ASCII='" not in reader and "HEX=0x%02X" not in reader


def test_maintenance_probe_policy_is_in_ci():
    workflow = (ROOT / ".github/workflows/esp-ota-ci.yml").read_text()
    assert "tools/test_scanner_safety_contract.py" in workflow
