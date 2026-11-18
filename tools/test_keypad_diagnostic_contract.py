"""Static guards for isolated PCF8574 keypad diagnostic mode."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "esp" / "mesflow_app.cpp").read_text()

def section(start, end):
    a = SOURCE.index(start)
    b = SOURCE.index(end, a)
    return SOURCE[a:b]

def test_keypad_mode_ignores_business_scans():
    scan = section("void handleSerialLine(String line) {", "String pendingCmd = line;")
    assert 'if (keypadTestActive)' in scan
    assert 'return;' in scan.split('if (keypadTestActive)', 1)[1]
    keypad = section("void handleKeypadKey(char key) {", "if (maintenanceMode)")
    assert "handleKeypadTestKey(key); return;" in keypad

def test_unmapped_raw_scan_and_timeout():
    runner = section("void serviceKeypad() {", "const uint8_t stateNow")
    assert "if (!keypadTestActive) return;" in runner
    assert "scanKeypadPair()" in runner
    assert "10UL * 60UL * 1000UL" in runner

def test_maintenance_and_lan_controls():
    assert 'cmd == "keypad-test confirm"' in SOURCE
    assert 'cmd == "keypad-test stop"' in SOURCE
    assert 'cmd == "keypad-calibrate confirm"' in SOURCE
    assert 'keypad_test_raw_pair' in SOURCE
    assert "Test kết nối &amp; nhập số" in SOURCE
    assert "keypad_test_digits" in SOURCE

def test_test_state_no_mesflow_event_calls():
    body = section("static void handleKeypadTestKey(char key) {",
                   "static bool detectBootWifiSetupShortcut() {")
    for forbidden in ("emitActionEvent(", "sendHeartbeat(", "handleSerialLine(",
                      "syncPendingTransaction(", "startSession(", "finishSession("):
        assert forbidden not in body
