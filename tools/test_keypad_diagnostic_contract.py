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
    assert "keypadTestRawLastPoll < 85" in runner
    assert "keypadTestLastI2cWarning > 5000" in runner

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


def test_mapped_keypad_starts_on_number_input_page():
    body = section("static bool startKeypadTestMode() {", "static void stopKeypadTestMode() {")
    assert "keypadTestPage = keypadMappingReady ? 1 : 0;" in body


def test_test_loop_isolation_from_mes_and_ready_redraw():
    body = section("void loop() {", "serviceStateWatchdog();")
    assert "if (keypadTestActive) {" in body
    assert "serviceKeypad();" in body.split("if (keypadTestActive) {",1)[1]
    assert "return;" in body.split("if (keypadTestActive) {",1)[1]
    assert "maintainConnection();" not in body.split("if (keypadTestActive) {",1)[1].split("return;",1)[0]

def test_number_entry_uses_fast_partial_lcd_redraw():
    body = section("static void handleKeypadTestKey(char key) {", "static bool detectBootWifiSetupShortcut() {")
    assert "drawKeypadTestNumberValue()" in body
    draw = section("static void drawKeypadTestNumberValue() {", "static bool startKeypadTestMode() {")
    assert "tft.fillRect" in draw
    assert "tft.fillScreen" not in draw


def test_fast_mapped_scan_preserves_original_calibration_path():
    reader = section("static int scanMappedKeypadFast() {",
                     "static void printKeypadPair(uint8_t pair) {")
    assert "degree[pin] == 3" in reader
    assert "degree[pin] == 4" in reader
    assert "rowMask" in reader and "colMask" in reader
    assert "keypadReleaseAll()" in reader
    assert "return count == 1 ? found : -2;" in reader
    loop = section("void serviceKeypad() {", "if (pair == -3)")
    assert "keypadTestActive ? scanMappedKeypadFast() : scanKeypadPair()" in loop

def test_live_latency_metrics_exposed():
    for key in ("keypad_test_scan_us", "keypad_test_scan_max_us",
                "keypad_test_draw_us", "keypad_test_draw_max_us",
                "keypad_test_loop_gap_max_ms"):
        assert f'hardware["{key}"]' in SOURCE

def test_fast_number_entry_prioritizes_physical_keypad_before_web():
    body = section("void loop() {", "// Display the server-confirmed FINISH")
    assert "serviceKeypad();" in body
    fast = body[body.index("if (keypadTestActive) {"):]
    assert fast.index("serviceKeypad();") < fast.index("serviceDeviceManagement();")
    assert fast.index("serviceDeviceManagement();") < fast.index("return;")
    assert "keypadTestWebLastMs >= 120" in fast
    assert "keypadTestLoopMaxGapMs" in fast

def test_fast_lcd_avoids_full_redraw_and_tracks_duration():
    body = section("static void drawKeypadTestNumberValue() {",
                   "static bool startKeypadTestMode() {")
    assert "tft.print(digits" in body
    assert "tft.fillScreen(" not in body
    assert "keypadTestDrawLastUs = micros()" in body
