"""Static regression contract for USB provisioning config preservation."""
from pathlib import Path

ROOT = Path(__file__).parents[1]
SOURCE = (ROOT / "esp/mesflow_app.cpp").read_text()


def body(signature: str) -> str:
    start = SOURCE.index(signature)
    opening = SOURCE.index("{", start)
    depth = 0
    for end in range(opening, len(SOURCE)):
        depth += {"{": 1, "}": -1}.get(SOURCE[end], 0)
        if depth == 0:
            return SOURCE[opening:end + 1]
    raise AssertionError(f"unterminated function: {signature}")


def test_config_refuses_existing_nonfactory_primary_configuration():
    config = body("static bool handleUsbProvisionConfig(const String& encoded) {")
    assert "configValueUnchangedOrFactoryDefault" in config
    for key in ("wifi_ssid", "wifi_pass", "server", "station", "device_id"):
        assert f'getString("{key}"' in config
    assert "Existing config differs; refusing overwrite." in config


def test_usb_config_write_is_scoped_and_readback_verified():
    config = body("static bool handleUsbProvisionConfig(const String& encoded) {")
    for key in ("wifi_ssid", "wifi_pass", "server", "station", "device_id"):
        assert f'putString("{key}"' in config
        assert f'getString("{key}"' in config
    for forbidden in ('putString("ota_agent"', 'putString("wifi_ssid2"', 'putString("wifi_pass2"',
                      'putString("wifi_ssid3"', 'putString("wifi_pass3"', 'putString("device_uuid"',
                      'putString("device_secret"', 'putString("token"', 'putString("alt_token"'):
        assert forbidden not in config
    assert "rollback verification failed; device remains unbound" in config


def test_config_requires_https_exact_identity_and_fresh_unbound_idle_device():
    config = body("static bool handleUsbProvisionConfig(const String& encoded) {")
    for requirement in ("mayAutoConfigure", 'server.startsWith("https://")', "expected_uuid",
                        'doc["mac_address"]', "DEVICE_UUID[0]", "DEVICE_ID"):
        assert requirement in config
    proof = body("static bool handleUsbProvisionProof(const String& challenge) {")
    assert "mbedtls_md_hmac" in proof and "DEVICE_SECRET" in proof
    assert 'Serial.print("MESFLOW_USB_PROOF:")' in proof
