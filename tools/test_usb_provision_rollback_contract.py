"""Static rollback contract for USB configuration writes; not hardware proof."""
from pathlib import Path


ROOT = Path(__file__).parents[1]
SOURCE = (ROOT / "esp/mesflow_app.cpp").read_text()


def _body(signature: str) -> str:
    start = SOURCE.index(signature)
    opening = SOURCE.index("{", start)
    depth = 0
    for end in range(opening, len(SOURCE)):
        depth += {"{": 1, "}": -1}.get(SOURCE[end], 0)
        if depth == 0:
            return SOURCE[opening:end + 1]
    raise AssertionError(f"unterminated function: {signature}")


def test_failed_usb_config_readback_restores_and_verifies_previous_values():
    config = _body("static bool handleUsbProvisionConfig(const String& encoded) {")
    failure = config[config.index("if (!saveDeviceConfig() || !loadDeviceConfig()") :]
    for old_value in ("oldSsid", "oldPass", "oldServer", "oldId", "oldName", "oldStation"):
        assert old_value in failure
    assert "saveDeviceConfig() && loadDeviceConfig()" in failure
    assert "rollback verification failed; device remains unbound" in failure


def test_success_requires_nvs_readback_of_all_new_configuration_fields():
    config = _body("static bool handleUsbProvisionConfig(const String& encoded) {")
    success_gate = config[config.index("if (!saveDeviceConfig() || !loadDeviceConfig()") :
                          config.index("safeCopy(WIFI_SSID", config.index("if (!saveDeviceConfig()"))]
    for field in ("WIFI_SSID", "WIFI_PASSWORD", "SERVER_BASE", "STATION_CODE", "DEVICE_ID"):
        assert f"strcmp({field}," in success_gate

