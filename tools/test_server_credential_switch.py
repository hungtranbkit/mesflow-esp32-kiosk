"""Source contract: kiosk credentials are scoped per MESFlow server, a server
change never deletes the production credential, bind shows the enrollment
code, and LAN provisioning needs maintenance (not a hardware test)."""
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


def test_both_config_paths_go_through_server_switch_before_replacing_url():
    api = body("static void handleMaintenanceApiUrl() {")
    assert api.index("applyServerChange(url, credError)") < api.index("safeCopy(SERVER_BASE")
    assert "409" in api
    cfg = body("static bool applyConfigJson(JsonDocument& doc, bool allowWifi, String& error) {")
    assert cfg.index("applyServerChange(server, error)") < cfg.index("copyConfigValue(SERVER_BASE")
    # server change no longer counts as an identity change that wipes the token
    ident = cfg[cfg.index("bool identityChanged"):cfg.index(";", cfg.index("bool identityChanged"))]
    assert "SERVER_BASE" not in ident


def test_switch_stashes_or_restores_never_deletes_spare():
    sw = body("static bool applyServerChange(const String& newServer, String& error) {")
    assert "mfcred::decideSwitch(" in sw
    stash = sw[sw.index("STASH_AND_UNBIND) {"):sw.index("RESTORE_FROM_SPARE) {")]
    assert 'putString("alt_token", rt.kioskToken)' in stash and "rt.bound = false" in stash
    restore = sw[sw.index("RESTORE_FROM_SPARE) {"):]
    assert 'putString("alt_token", curTok)' in restore and "rt.bound = true" in restore
    assert "hasPendingTransaction()" in sw and "countPendingOfflineEvents()" in sw
    for bad in ("clearNamespace", "clearPendingTransaction", "LittleFS.remove", "WiFi."):
        assert bad not in sw, bad


def test_token_is_bound_only_for_its_issuing_server():
    setup = body("void setup() {")
    assert "mfcred::tokenUsableFor(true, storedTokenServer.c_str(), SERVER_BASE)" in setup
    assert 'putString("token_srv", SERVER_BASE)' in body("static void saveCurrentCredential() {")
    assert "saveCurrentCredential();" in body("bool bindKiosk() {")


def test_bind_shows_enrollment_code_and_still_sends_device_secret():
    bind = body("bool bindKiosk() {")
    assert 'request["device_secret"] = DEVICE_SECRET;' in bind
    assert '"KIOSK_ENROLLMENT_PENDING"' in bind and "computeEnrollmentCode(local)" in bind
    assert "DEVICE_SECRET" not in bind[bind.index("KIOSK_ENROLLMENT_PENDING"):]  # never logged
    ready = body("void drawReady(uint8_t frame = 0) {")
    assert "enrollmentCode" in ready and "!rt.bound && enrollmentPending" in ready


def test_lan_provision_requires_maintenance_and_token_not_published_otherwise():
    assert "return maintenanceMode && provisionWeb.hasHeader" in body("static bool provisionAuthorized() {")
    for sig in ("static void sendDeviceInfo(WebServer& web) {", "static void broadcastDiscovery() {"):
        b = body(sig)
        assert 'if (maintenanceMode) doc["provision_token"] = provisionToken;' in b
        assert b.count('doc["provision_token"]') == 1
