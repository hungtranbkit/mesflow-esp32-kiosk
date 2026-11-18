#pragma once
// Pure keypad boot/calibration policy (no Arduino dependency), host tested in
// tests/host/test_keypad_policy.cpp.
//
// v5.6.2/v5.6.3 root cause: setup() ran initKeypad() before Wi-Fi and, with no
// mapping in NVS, entered calibrateKeypadInteractive(), which waited forever
// for key presses (and starved the 40 s loop watchdog). The kiosk never got
// network, so it could never be reached by OTA. Rules now:
//  * a missing/invalid mapping NEVER blocks boot: the keypad stays disabled
//    and Wi-Fi, MESFlow, OTA and the GM65 scanner come up normally;
//  * calibration starts ONLY on an explicit request (maintenance console /
//    serial), never automatically at boot, on hot-plug or on unknown keys;
//  * an explicit calibration is bounded (idle timeout, limited rounds) and
//    keeps the previous mapping when it does not finish -- no fake mapping.
#include <stdint.h>

namespace mfkp {

enum class KeypadState : uint8_t { ABSENT = 0, READY = 1, DISABLED_NEEDS_CALIBRATION = 2 };

inline KeypadState bootKeypadState(bool present, bool mappingValid) {
  if (!present) return KeypadState::ABSENT;
  return mappingValid ? KeypadState::READY : KeypadState::DISABLED_NEEDS_CALIBRATION;
}

// Keys are only interpreted with a valid, saved mapping.
inline bool keypadInputEnabled(KeypadState s) { return s == KeypadState::READY; }

// Boot, hot-plug and unknown key pairs never start calibration by themselves.
inline bool mayStartCalibration(bool explicitRequest, bool keypadPresent, bool uiReady, bool pendingTransaction) {
  return explicitRequest && keypadPresent && uiReady && !pendingTransaction;
}

static const uint32_t CALIBRATION_IDLE_TIMEOUT_MS = 60UL * 1000UL;  // no key activity
static const uint8_t CALIBRATION_MAX_ROUNDS = 3;                     // invalid 3x4 matrix retries

// millis() wrap-safe.
inline bool calibrationTimedOut(uint32_t lastActivityMs, uint32_t nowMs) {
  return (uint32_t)(nowMs - lastActivityMs) >= CALIBRATION_IDLE_TIMEOUT_MS;
}

}  // namespace mfkp
