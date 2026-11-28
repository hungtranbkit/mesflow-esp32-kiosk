"""Contracts restored while consolidating firmware lineage for release."""
from pathlib import Path

SOURCE = (Path(__file__).resolve().parents[1] / "esp/mesflow_app.cpp").read_text()


def test_scanner_splits_terminator_delimited_frames_without_losing_utf8_policy():
    reader = SOURCE.split("void readScannerCommands() {", 1)[1].split(
        "static bool keypadWriteRead", 1
    )[0]
    assert "if (value == '\\r' || value == '\\n')" in reader
    assert 'dispatchScannerFrame("CR_LF")' in reader
    assert "mfqr::isDataByte(value)" in SOURCE


def test_power_loss_recovers_completed_action_queue_temp_file_on_boot():
    setup = SOURCE.split("void setup() {", 1)[1]
    assert "fsReady = LittleFS.begin(true);" in setup
    recovery = setup.split("fsReady = LittleFS.begin(true);", 1)[1].split(
        "prefs.begin(\"mesflow\", true);", 1
    )[0]
    assert "LittleFS.exists(ACTION_QUEUE_TMP_PATH)" in recovery
    assert "LittleFS.rename(ACTION_QUEUE_TMP_PATH, ACTION_QUEUE_PATH)" in recovery


def test_offline_opid_finish_and_pending_finish_reconcile_with_server_sessions():
    assert "findOfflineSessionForOp(rt.workerQr,rt.operationQr,rt.operationId)" in SOURCE
    assert "finishPendingFor(rt.workerQr, o.opQr, o.operationId)" in SOURCE
    assert "operationIdFromQr(offlineSessions[i].operationQr)==opId" in SOURCE


def test_keypad_cancel_clears_local_selection_without_erasing_open_session_data():
    keypad = SOURCE.split("void handleKeypadKey(char key) {", 1)[1].split(
        "static void", 1
    )[0]
    assert "key == '*' && (uiState == UiState::WORKER_OK || uiState == UiState::OPERATION_OK)" in keypad
    assert "resetForNextWorker();" in keypad
    assert "clearOfflineSessions" not in keypad

