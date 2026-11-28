from pathlib import Path
import re

SOURCE=(Path(__file__).parents[1]/"esp/mesflow_app.cpp").read_text()
POLICY=(Path(__file__).parents[1]/"esp/mesflow_ota_policy.h").read_text()


def test_integrated_release_identity_advances_v5617():
    assert '#define FW_VERSION "5.6.17"' in SOURCE
    assert '#define FW_BUILD "20261128.0007"' in SOURCE
    assert 'const char* APP_VERSION  = "ESP32-KIOSK-5.6.17-KIMEX-INTEGRATED"' in SOURCE


def test_identity_and_partition_safe_contract():
    assert 'MESFLOW_FW_IDENTITY:version=" FW_VERSION' in SOURCE
    assert '#define HW_MODEL "ES3C28P"' in SOURCE
    assert 'esp_ota_get_next_update_partition' in SOURCE
    assert 'otaExpectedSize > next->size' in SOURCE


def test_no_update_and_rate_limit_contract():
    assert 'OTA_CHECK_INTERVAL_MS' in SOURCE and 'OTA_RETRY_INTERVAL_MS' in SOURCE
    assert 'if (!(response["update_available"] | false))' in SOURCE


def test_active_session_quantity_scan_and_offline_queue_wait():
    # v5.6.3: in-flight ESP data always blocks; a server-durable work session
    # only blocks non-forced updates (behaviour unit-tested in tests/host).
    block=SOURCE[SOURCE.index('static mfota::SafePoint otaSafePoint()'):SOURCE.index('static void otaReportWait(')]
    assert 'uiState == UiState::READY' in block
    assert 'rt.activeSessionId > 0' in block
    assert 'countPendingOfflineEvents()' in block
    assert 'actionQueueCount()' in block
    assert 'hasPendingTransaction()' in block
    assert 'in.longTermSession && !in.force' in POLICY
    assert 'in.pendingTransaction || in.offlineEvents || in.actionQueue || in.offlineMode' in POLICY


def test_download_failures_and_success_path_are_distinct():
    for code in ('OTA_NO_SPACE','OTA_NETWORK_ERROR','OTA_HTTP_ERROR','OTA_SIZE_MISMATCH','OTA_HASH_MISMATCH','OTA_FLASH_ERROR','OTA_WRONG_HARDWARE'):
        assert code in SOURCE
    assert 'OTA_DOWNLOAD_COMPLETE' in SOURCE and 'OTA_VERIFY_OK' in SOURCE and 'OTA_REBOOTING' in SOURCE


def test_boot_validation_and_rollback_support():
    assert 'ESP_OTA_IMG_PENDING_VERIFY' in SOURCE
    assert 'esp_ota_mark_app_valid_cancel_rollback' in SOURCE
    assert 'OTA_BOOT_NEW_VERSION' in SOURCE and 'OTA_HEALTHCHECK_OK' in SOURCE


def test_pending_ota_ack_is_stable_and_observes_application_result():
    event = SOURCE[SOURCE.index('static bool otaEvent('):SOURCE.index('static mfota::SafePoint otaSafePoint()')]
    assert 'if (otaBootReportPending)' in event
    assert 'otaTargetId' in event and 'otaFirmwareId' in event and 'otaClaimId' in event
    assert 'mfota::eventAcknowledged(ok, resp.containsKey("ok"), resp["ok"] | false' in event
    assert 'resp.containsKey("applied")' in event


def test_post_json_never_logs_request_body():
    post = SOURCE[SOURCE.index('bool httpPostJson('):SOURCE.index('static bool otaEvent(')]
    assert 'Serial.println(body)' not in post


def test_header_clock_and_signal_regions_do_not_overlap():
    # Check geometric boundaries, not a fixed brand label or clock position.
    def value(name):
        return int(re.search(r"constexpr int16_t " + name + r" = (\d+);", SOURCE).group(1))
    assert value("CLOCK_X") + value("CLOCK_W") <= value("NET_LED_X") - 10
    assert value("CLOCK_Y") + value("CLOCK_H") < value("HEADER_H")
    assert value("NET_LED_X") + 15 <= value("SW")
