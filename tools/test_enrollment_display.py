#!/usr/bin/env python3
"""Run real bindKiosk/idle-refresh C++ bodies with fake HTTP, NVS and LCD edges."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "esp/mesflow_app.cpp"

def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    for i in range(opening + 1, len(source)):
        depth += (source[i] == "{") - (source[i] == "}")
        if not depth:
            return source[start:i + 1]
    raise AssertionError("unclosed function " + signature)

PREFIX = r"""
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#if __has_include("mesflow_device_identity.h")
#include "mesflow_device_identity.h"
#define IDENTITY_POLICY 1
mfidentity::DisplayChanges identityDisplayChanges;
#endif
enum class UiState { BOOT, WIFI, BINDING, READY, OFFLINE, INPUT_GOOD };
UiState uiState = UiState::READY;
struct Runtime {
  char kioskToken[192] = "", lastError[128] = "";
  bool bound = false, online = true, hasWorker = false, hasOperation = false;
  int lastHttpStatus = 200, activeSessionId = 0;
} rt;
char DEVICE_UUID[40] = "0f2d2ad3-06d7-417f-a1e7-da3d2457db39";
char DEVICE_SECRET[65] = "host-fixture-only", DEVICE_ID[64] = "legacy";
char DEVICE_NAME[64] = "Máy 2", STATION_CODE[40] = "LASER-01";
char otaRunningArtifact[64] = "";
constexpr const char* FW_VERSION = "test";
constexpr const char* FW_BUILD_ID = "test";
constexpr const char* HW_MODEL = "ES3C28P";
constexpr const char* HW_PROFILE = "test-profile";
constexpr int HEX = 16;
unsigned bootId = 1;
std::string String(unsigned value, int = 10) { return std::to_string(value); }
int esp_reset_reason() { return 1; }
const void* inactiveOtaPartition() { return nullptr; }
bool enrollmentPending = true, suppressNetworkUiErrors = false;
char enrollmentCode[7] = "74C150";
bool credentialSaveOk = true, credentialSaved = false, posted = true;
bool maintenanceMode = false, setupPortalActive = false;
bool keypadCalibrationRequested = false, keypadCalibrationInProgress = false;
bool keypadTestActive = false, otaInstallInProgress = false, otaCheckTaskRunning = false;
bool recoveryMenuActive = false, pendingTransaction = false;
unsigned queuedEvents = 0, telemetry = 0, openOpCount = 0;
int repaints = 0, headerDraws = 0;
unsigned journalReads = 0;
std::string pixels = "CHỜ DUYỆT KIOSK";
struct SerialStub {
  template<class... T> void printf(const char*, T...) {}
  void println(const char*) {}
} Serial;
template<class... T> void remoteLogf(const char*, T...) {}
void safeCopy(char* dest, size_t capacity, const char* value) {
  std::snprintf(dest, capacity, "%s", value);
}
void drawTopClock(bool) {}
void drawIndustrialHeader() {
  ++headerDraws;
#ifdef IDENTITY_POLICY
  identityDisplayChanges.drawn();
#endif
}
void redrawProductionUi() {
  ++repaints;
  pixels = enrollmentPending && !rt.bound ? "CHỜ DUYỆT KIOSK" : "SCAN";
  drawIndustrialHeader();
}
void setUi(UiState state) { uiState = state; redrawProductionUi(); }
void setError(int code, const char* text) {
  rt.lastHttpStatus = code; safeCopy(rt.lastError, sizeof(rt.lastError), text);
}
bool saveCurrentCredential() { credentialSaved = credentialSaveOk; return credentialSaveOk; }
bool saveDeviceConfig() { return true; }
void computeEnrollmentCode(char out[7]) { std::strcpy(out, "74C150"); }
bool hasPendingTransaction() { return pendingTransaction; }
unsigned countPendingOfflineEvents() { ++journalReads; return queuedEvents; }
unsigned actionQueueCount() { return telemetry; }
struct Value {
  std::string value;
  template<class T> Value& operator=(T v) {
    std::ostringstream out; out << v; value = out.str(); return *this;
  }
  const char* operator|(const char* fallback) const { return value.empty() ? fallback : value.c_str(); }
};
struct DynamicJsonDocument {
  std::map<std::string, Value> fields;
  explicit DynamicJsonDocument(int) {}
  Value& operator[](const char* key) { return fields[key]; }
};
DynamicJsonDocument reply(1024);
bool httpPostJson(const char*, DynamicJsonDocument&, DynamicJsonDocument& out, bool) {
  out = reply; return posted;
}
bool adoptServerDeviceName(const Value&) { return false; }
bool adoptServerDisplayNumber(const Value&) { return false; }
"""

CASES = r"""
int main() {
  reply["kiosk_token"] = "new-host-fixture";
  reply["station_code"] = "LASER-01";
  // The production failure: background approval changed flags but left pending pixels.
  assert(bindKiosk());
  assert(credentialSaved && rt.bound && !enrollmentPending && !enrollmentCode[0]);
  assert(uiState == UiState::READY && repaints == 1 && pixels == "SCAN");

  // Failed durable writes must leave approval pending, with the screen intact.
  rt.bound = false; std::strcpy(rt.kioskToken, "previous-token");
  enrollmentPending = true; std::strcpy(enrollmentCode, "74C150");
  credentialSaveOk = false; pixels = "CHỜ DUYỆT KIOSK"; repaints = 0;
  assert(!bindKiosk());
  assert(!rt.bound && enrollmentPending && std::strcmp(enrollmentCode, "74C150") == 0);
  assert(std::strcmp(rt.kioskToken, "previous-token") == 0 && repaints == 0);

  // Rebinding during a quantity entry never clears the operator's workflow.
  credentialSaveOk = true; uiState = UiState::INPUT_GOOD;
  rt.hasWorker = rt.hasOperation = true; pixels = "QUANTITY 22"; repaints = 0;
  assert(bindKiosk());
  assert(uiState == UiState::INPUT_GOOD && rt.hasWorker && rt.hasOperation);
  assert(repaints == 0 && pixels == "QUANTITY 22");
#ifdef IDENTITY_POLICY
  // The queued repaint waits for idle, and is consumed only by a real draw.
  uiState = UiState::READY;
  refreshDeviceIdentityDisplay();
  assert(repaints == 0);
  rt.hasWorker = rt.hasOperation = false;
  refreshDeviceIdentityDisplay();
  assert(repaints == 1 && pixels == "SCAN");
  refreshDeviceIdentityDisplay();
  assert(repaints == 1);

  // Approval while OFFLINE clears the banner without changing logical state.
  uiState = UiState::OFFLINE; rt.bound = false; enrollmentPending = true;
  std::strcpy(enrollmentCode, "74C150"); repaints = 0;
  assert(bindKiosk());
  assert(uiState == UiState::OFFLINE && repaints == 1 && pixels == "SCAN");
#endif
#ifdef IDENTITY_POLICY
  // loop() invokes the display service every tick; unchanged screens must
  // not open/scan the LittleFS journal just to discover nothing needs drawing.
  journalReads = 0;
  refreshDeviceIdentityDisplay();
  assert(journalReads == 0);

  identityDisplayChanges.renamed();
  uiState = UiState::INPUT_GOOD;
  refreshDeviceIdentityDisplay();
  assert(journalReads == 0);
  assert(identityDisplayChanges.needed(true, false) == mfidentity::Refresh::HEADER);
  uiState = UiState::READY;
  maintenanceMode = true;
  refreshDeviceIdentityDisplay();
  assert(journalReads == 0);
  maintenanceMode = false;
  refreshDeviceIdentityDisplay();
  assert(journalReads == 1);
  assert(identityDisplayChanges.needed(true, false) == mfidentity::Refresh::NONE);
#endif

  // A newly received number follows the same guarded header-only path.
  identityDisplayChanges.numbered();
  uiState = UiState::INPUT_GOOD;
  rt.hasWorker = rt.hasOperation = true;
  pixels = "QUANTITY 22";
  const int beforeHeaders = headerDraws;
  journalReads = 0;
  refreshDeviceIdentityDisplay();
  assert(headerDraws == beforeHeaders && pixels == "QUANTITY 22" && journalReads == 0);
  assert(rt.hasWorker && rt.hasOperation && uiState == UiState::INPUT_GOOD);
  uiState = UiState::READY;
  refreshDeviceIdentityDisplay();
  assert(headerDraws == beforeHeaders && journalReads == 0);
  rt.hasWorker = rt.hasOperation = false;
  refreshDeviceIdentityDisplay();
  assert(headerDraws == beforeHeaders + 1 && journalReads == 1);
  assert(identityDisplayChanges.needed(true, false) == mfidentity::Refresh::NONE);

  puts("PASS approval, persistence, workflow preservation, deferred idle and zero idle journal scans");
}
"""

class EnrollmentDisplayTests(unittest.TestCase):
    def test_background_approval_clears_real_bind_screen_only_when_safe(self):
        source = SOURCE.read_text()
        refresh = ""
        if "static void refreshDeviceIdentityDisplay() {" in source:
            refresh = function(source, "static void refreshDeviceIdentityDisplay() {")
        harness = PREFIX + refresh + "\n" + function(source, "bool bindKiosk() {") + CASES
        with tempfile.TemporaryDirectory() as tmp:
            cpp = Path(tmp) / "bind_test.cpp"
            binary = Path(tmp) / "bind_test"
            cpp.write_text(harness)
            built = subprocess.run(
                ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "esp"),
                 str(cpp), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stderr)
            print(ran.stdout.strip())

if __name__ == "__main__":
    unittest.main()
