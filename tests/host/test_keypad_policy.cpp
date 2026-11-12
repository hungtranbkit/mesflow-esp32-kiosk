// Host unit tests for esp/mesflow_keypad_policy.h (pure logic, not a hardware test).
// Build+run: g++ -std=c++17 -Wall -Wextra -Werror -I esp tests/host/test_keypad_policy.cpp -o /tmp/k && /tmp/k
#include <cstdio>
#include "mesflow_keypad_policy.h"

using namespace mfkp;
static int fails = 0, total = 0;
#define CHECK(c) do { total++; if (!(c)) { fails++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int main() {
  // Regression (v5.6.2/v5.6.3): no mapping in NVS must not block boot.
  CHECK(bootKeypadState(true, false) == KeypadState::DISABLED_NEEDS_CALIBRATION);
  CHECK(!keypadInputEnabled(bootKeypadState(true, false)));       // no fake mapping: keys ignored
  CHECK(bootKeypadState(false, false) == KeypadState::ABSENT);
  CHECK(bootKeypadState(true, true) == KeypadState::READY);
  CHECK(keypadInputEnabled(bootKeypadState(true, true)));
  // Calibration only on explicit request, never automatic (boot/hot-plug/unknown key).
  CHECK(!mayStartCalibration(false, true, true, false));
  CHECK(mayStartCalibration(true, true, true, false));
  CHECK(!mayStartCalibration(true, false, true, false));           // no PCF8574T
  CHECK(!mayStartCalibration(true, true, false, false));           // kiosk not READY
  CHECK(!mayStartCalibration(true, true, true, true));             // pending transaction
  // Bounded explicit calibration, millis() wrap-safe.
  CHECK(!calibrationTimedOut(1000, 1000 + CALIBRATION_IDLE_TIMEOUT_MS - 1));
  CHECK(calibrationTimedOut(1000, 1000 + CALIBRATION_IDLE_TIMEOUT_MS));
  CHECK(calibrationTimedOut(0xFFFFFF00UL, (uint32_t)(0xFFFFFF00UL + CALIBRATION_IDLE_TIMEOUT_MS)));
  CHECK(!calibrationTimedOut(0xFFFFFF00UL, 0x00000010UL));
  CHECK(CALIBRATION_IDLE_TIMEOUT_MS < 40000UL * 2 && CALIBRATION_MAX_ROUNDS >= 1);
  std::printf("%d/%d checks passed\n", total - fails, total);
  return fails ? 1 : 0;
}
