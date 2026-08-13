from pathlib import Path

SOURCE=(Path(__file__).parents[1]/"esp/mesflow_app.cpp").read_text()


def test_identity_and_partition_safe_contract():
    assert '#define FW_VERSION "5.5.7"' in SOURCE
    assert '#define FW_BUILD "20260813.0015"' in SOURCE
    assert '#define HW_MODEL "ES3C28P"' in SOURCE
    assert 'esp_ota_get_next_update_partition' in SOURCE
    assert 'otaExpectedSize > next->size' in SOURCE


def test_no_update_and_rate_limit_contract():
    assert 'OTA_CHECK_INTERVAL_MS' in SOURCE and 'OTA_RETRY_INTERVAL_MS' in SOURCE
    assert 'if (!(response["update_available"] | false))' in SOURCE


def test_active_session_quantity_scan_and_offline_queue_wait():
    block=SOURCE[SOURCE.index('static bool otaIdleSafe()'):SOURCE.index('static void rememberOtaBoot()')]
    assert 'uiState == UiState::READY' in block
    assert 'rt.activeSessionId <= 0' in block
    assert 'countPendingOfflineEvents() == 0' in block
    assert 'actionQueueCount() == 0' in block
    assert '!hasPendingTransaction()' in block


def test_download_failures_and_success_path_are_distinct():
    for code in ('OTA_NO_SPACE','OTA_NETWORK_ERROR','OTA_HTTP_ERROR','OTA_SIZE_MISMATCH','OTA_HASH_MISMATCH','OTA_FLASH_ERROR','OTA_WRONG_HARDWARE'):
        assert code in SOURCE
    assert 'OTA_DOWNLOAD_COMPLETE' in SOURCE and 'OTA_VERIFY_OK' in SOURCE and 'OTA_REBOOTING' in SOURCE


def test_boot_validation_and_rollback_support():
    assert 'ESP_OTA_IMG_PENDING_VERIFY' in SOURCE
    assert 'esp_ota_mark_app_valid_cancel_rollback' in SOURCE
    assert 'OTA_BOOT_NEW_VERSION' in SOURCE and 'OTA_HEALTHCHECK_OK' in SOURCE


def test_header_layout_contract():
    assert 'constexpr int16_t CLOCK_X = 88;' in SOURCE
    assert 'constexpr int16_t NET_LED_X = 220;' in SOURCE
    assert 'printText(10, 10, "Kimex"' in SOURCE
