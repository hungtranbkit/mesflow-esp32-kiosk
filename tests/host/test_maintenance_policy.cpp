// Host unit tests for esp/mesflow_maintenance_policy.h (pure logic, not a hardware test).
// Build+run: g++ -std=c++17 -Wall -Wextra -Werror -I esp tests/host/test_maintenance_policy.cpp -o /tmp/m && /tmp/m
#include <cstdio>
#include "mesflow_maintenance_policy.h"

using namespace mfmt;
static int fails = 0, total = 0;
#define CHECK(c) do { total++; if (!(c)) { fails++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int main() {
  // Explicit phrase only.
  CHECK(isMaintenanceConfirm("maintenance confirm"));
  CHECK(!isMaintenanceConfirm("maintenance"));
  CHECK(!isMaintenanceConfirm("maintenance confirmx"));
  CHECK(!isMaintenanceConfirm("scan maintenance confirm"));
  CHECK(!isMaintenanceConfirm("maintenance  confirm"));
  CHECK(!isMaintenanceConfirm(""));
  CHECK(!isMaintenanceConfirm(nullptr));
  // Entry only from a quiet screen; never during OTA/pending journal/calibration.
  CHECK(serialEntryDecision(false, true, false, false, false) == EntryDecision::ALLOW);
  CHECK(serialEntryDecision(true, true, false, false, false) == EntryDecision::ALREADY_ACTIVE);
  CHECK(serialEntryDecision(false, true, false, true, false) == EntryDecision::OTA_IN_PROGRESS);
  CHECK(serialEntryDecision(false, true, true, false, false) == EntryDecision::PENDING_TRANSACTION);
  CHECK(serialEntryDecision(false, true, false, false, true) == EntryDecision::CALIBRATION_ACTIVE);
  CHECK(serialEntryDecision(false, false, false, false, false) == EntryDecision::NOT_IDLE_SCREEN);
  CHECK(serialEntryDecision(false, false, true, true, true) == EntryDecision::OTA_IN_PROGRESS);
  // While in maintenance: no QR/session or admin input, only exit/status/help.
  CHECK(allowedDuringMaintenance("maintenance exit"));
  CHECK(allowedDuringMaintenance("status"));
  CHECK(allowedDuringMaintenance("help"));
  CHECK(!allowedDuringMaintenance("factory-reset"));
  CHECK(!allowedDuringMaintenance("clear-offline"));
  CHECK(!allowedDuringMaintenance("WF|EMP|E001"));
  CHECK(!allowedDuringMaintenance("statusx"));
  CHECK(!allowedDuringMaintenance(nullptr));
  // Idle timeout, millis() wrap-safe.
  CHECK(!maintenanceTimedOut(1000, 1000 + MAINTENANCE_IDLE_TIMEOUT_MS - 1));
  CHECK(maintenanceTimedOut(1000, 1000 + MAINTENANCE_IDLE_TIMEOUT_MS));
  CHECK(maintenanceTimedOut(0xFFFFFF00UL, (uint32_t)(0xFFFFFF00UL + MAINTENANCE_IDLE_TIMEOUT_MS)));
  CHECK(!maintenanceTimedOut(0xFFFFFF00UL, 0x00000010UL));
  CHECK(MAINTENANCE_IDLE_TIMEOUT_MS >= 60000UL && MAINTENANCE_IDLE_TIMEOUT_MS <= 30UL * 60000UL);
  std::printf("%d/%d checks passed\n", total - fails, total);
  return fails ? 1 : 0;
}
