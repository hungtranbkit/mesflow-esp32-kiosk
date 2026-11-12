"""Source contract: maintenance entry is USB CDC serial only, bounded, and
does not touch Wi-Fi/OTA/scanner/pending-journal state (not a hardware test)."""
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


STATE_MUTATORS = ("saveDeviceConfig", "clearPendingTransaction", "savePendingTransaction", "clearNamespace",
                  "WiFi.disconnect", "WiFi.begin", "reattachScanner", "applyScannerBaudCommand",
                  "saveOtaAgentSource", "clearRuntimeSelection", "LittleFS.remove", "ESP.restart",
                  "consoleClear", "Update.", "esp_ota_")


def test_console_commands_only_from_usb_reader():
    # handleSerialLine also carries LAN `scan` payloads, GM65 frames and keypad digits.
    assert "handleConsoleCommand(" not in body("void handleSerialLine(String line) {")
    usb = body("static void handleUsbConsoleLine(String line) {")
    assert "handleConsoleCommand(line)" in usb
    reader = body("void readSerialCommands() {")
    assert "Serial.read()" in reader and reader.count("handleUsbConsoleLine(line)") == 2
    assert "handleSerialLine(" not in reader
    assert SOURCE.count("handleUsbConsoleLine(") == 3  # definition + 2 calls in the USB reader
    assert SOURCE.count("handleConsoleCommand(") == 2  # definition + USB console only


def test_no_lan_or_scanner_maintenance_command():
    remote = body("static String executeRemoteCommand(String line, bool& delayedReboot, bool& openSetup) {")
    for bad in ("maintenance", "enterMaintenanceMode", "handleConsoleCommand", "handleUsbConsoleLine",
                "openRecoveryMenu", "recovery-menu"):
        assert bad not in remote, bad
    scanner = body("void dispatchScannerFrame(const char* reason) {")
    assert "handleConsoleCommand" not in scanner and "handleUsbConsoleLine" not in scanner


def test_maintenance_confirm_is_explicit_and_gated():
    console = body("static bool handleConsoleCommand(String line) {")
    assert "mfmt::isMaintenanceConfirm(cmd.c_str())" in console
    enter = body("static void consoleEnterMaintenance() {")
    for need in ("hasPendingTransaction()", "otaInstallInProgress", "keypadCalibrationInProgress",
                 "UiState::READY", "mfmt::serialEntryDecision", "enterMaintenanceMode(false)"):
        assert need in enter, need


def test_maintenance_has_idle_timeout_and_usb_exit():
    svc = body("static void serviceMaintenanceMode() {")
    assert "mfmt::maintenanceTimedOut(lastMaintenanceActivityAt, millis())" in svc
    assert "exitMaintenanceMode()" in svc
    assert "lastMaintenanceActivityAt = millis();" in body("static void enterMaintenanceMode(bool duringBoot) {")
    usb = body("static void handleUsbConsoleLine(String line) {")
    assert "mfmt::allowedDuringMaintenance" in usb and "exitMaintenanceMode()" in usb
    loop = body("void loop() {")
    m = loop[loop.index("if (maintenanceMode) {"):]
    m = m[:m.index("return;")]
    assert "readSerialCommands();" in m and "serviceMaintenanceMode();" in m
    assert "servicePendingWebCommand" not in m  # LAN console stays off during maintenance


def test_entry_exit_preserve_wifi_ota_scanner_journal():
    for sig in ("static void consoleEnterMaintenance() {", "static void enterMaintenanceMode(bool duringBoot) {",
                "static void exitMaintenanceMode() {", "static void serviceMaintenanceMode() {",
                "static void handleUsbConsoleLine(String line) {"):
        b = body(sig)
        for bad in STATE_MUTATORS:
            assert bad not in b, (sig, bad)


def test_config_write_still_requires_maintenance_and_validation():
    api = body("static void handleMaintenanceApiUrl() {")
    assert api.index("if (!maintenanceMode)") < api.index("saveDeviceConfig()")
    assert "validServerBase(url)" in api and 'otaUrl.startsWith("https://")' in api
    assert ".setInsecure(" not in SOURCE
