"""Regression contract for the v5.6.2/v5.6.3 boot hang (source-level, not hardware).

Root cause: setup() -> initKeypad() ran before Wi-Fi; with no keypad mapping in
NVS it called calibrateKeypadInteractive(), an unbounded loop that also starved
the 40 s loop watchdog, so the kiosk never reached the network/OTA.
"""
import re
from pathlib import Path

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


def test_boot_never_calibrates_without_mapping():
    init = body("bool initKeypad() {")
    assert "calibrateKeypadInteractive" not in init
    assert "requestRuntimeKeypadCalibration" not in init
    assert "loadKeypadMapping()" in init


def test_setup_reaches_wifi_after_keypad_init_without_keypad_wait():
    setup = body("void setup() {")
    k = setup.index("initKeypad();")
    rest = setup[k:]
    assert "calibrateKeypadInteractive" not in rest and "waitStableKeypadPair" not in rest
    assert re.search(r"WiFi\.|connectWifi|startWifi|beginWifi", rest), "Wi-Fi must still be started after initKeypad()"


def test_runtime_never_auto_starts_calibration():
    presence = body("static void serviceKeypadPresence() {")
    assert "requestRuntimeKeypadCalibration" not in presence
    assert "calibrateKeypadInteractive" not in presence
    poll = body("void serviceKeypad() {")
    assert "requestRuntimeKeypadCalibration" not in poll and "calibrateKeypadInteractive" not in poll


def test_calibration_only_from_explicit_requests():
    callers = re.findall(r'requestRuntimeKeypadCalibration\("(\w+)"\)', SOURCE)
    assert sorted(set(callers)) == ["SERIAL", "WEB"]
    calls = [m.start() for m in re.finditer(r"calibrateKeypadInteractive\(\);", SOURCE)]
    service = body("static void serviceRuntimeKeypadCalibration() {")
    assert len(calls) == 1 and "calibrateKeypadInteractive();" in service


def test_explicit_calibration_is_bounded_and_keeps_old_mapping():
    cal = body("static bool calibrateKeypadInteractive() {")
    assert "while (true)" not in cal.split("for (uint8_t round", 1)[0]
    assert "CALIBRATION_MAX_ROUNDS" in cal and "== -4" in cal
    assert "loadKeypadMapping()" in cal          # restore stored mapping on failure
    for fn in ("static bool waitKeypadRelease() {", "static int waitStableKeypadPair() {"):
        b = body(fn)
        assert "esp_task_wdt_reset()" in b and "calibrationTimedOut" in b
    service = body("static void serviceRuntimeKeypadCalibration() {")
    assert "clearKeypadCalibration" not in service and "clearNamespace(KEYPAD_NVS_NAMESPACE)" not in service


def test_keys_ignored_without_mapping_and_scanner_hold_untouched():
    poll = body("void serviceKeypad() {")
    assert "!keypadMappingReady" in poll
    assert "MAX_OPEN_OPS" in SOURCE
