#pragma once
// Pure maintenance-entry policy (no Arduino dependency), host tested in
// tests/host/test_maintenance_policy.cpp.
//
// Why: a board with no keypad mapping (keypad disabled, v5.6.4) had no way into
// maintenance except a calibrated '*' hold. The explicit entry added here is
// the USB CDC serial line `maintenance confirm` (physical cable on the board).
// Rules:
//  * console/admin commands are accepted ONLY from the USB CDC serial reader;
//    LAN console `scan <payload>` and GM65 frames are QR input only and can
//    never reach a console command (closes the pre-v5.6.5 `scan recovery-menu`
//    / `scan factory-reset` path from the unauthenticated LAN console);
//  * entry only from a quiet READY/OFFLINE screen: no pending transaction, no
//    OTA install, no keypad calibration;
//  * maintenance does not relax any check: the API-URL page still validates
//    the URL, the OTA Agent URL must be https, and a new server still needs
//    bind + approval on MESFlow;
//  * maintenance ends by itself after MAINTENANCE_IDLE_TIMEOUT_MS without
//    activity, so a board is never left with the config page open.
#include <stdint.h>

namespace mfmt {

enum class EntryDecision : uint8_t {
  ALLOW = 0,
  ALREADY_ACTIVE,
  NOT_IDLE_SCREEN,
  PENDING_TRANSACTION,
  OTA_IN_PROGRESS,
  CALIBRATION_ACTIVE,
};

inline EntryDecision serialEntryDecision(bool alreadyActive, bool idleScreen, bool pendingTransaction,
                                         bool otaInstalling, bool calibrationActive) {
  if (alreadyActive) return EntryDecision::ALREADY_ACTIVE;
  if (otaInstalling) return EntryDecision::OTA_IN_PROGRESS;
  if (calibrationActive) return EntryDecision::CALIBRATION_ACTIVE;
  if (pendingTransaction) return EntryDecision::PENDING_TRANSACTION;
  if (!idleScreen) return EntryDecision::NOT_IDLE_SCREEN;
  return EntryDecision::ALLOW;
}

inline const char* entryDecisionName(EntryDecision d) {
  switch (d) {
    case EntryDecision::ALLOW: return "ALLOW";
    case EntryDecision::ALREADY_ACTIVE: return "ALREADY_ACTIVE";
    case EntryDecision::NOT_IDLE_SCREEN: return "NOT_IDLE_SCREEN";
    case EntryDecision::PENDING_TRANSACTION: return "PENDING_TRANSACTION";
    case EntryDecision::OTA_IN_PROGRESS: return "OTA_IN_PROGRESS";
    case EntryDecision::CALIBRATION_ACTIVE: return "CALIBRATION_ACTIVE";
  }
  return "UNKNOWN";
}

// Only the exact, explicit phrase enters maintenance (case/space-insensitive
// at the ends is handled by the caller's trim()+toLowerCase()).
inline bool isMaintenanceConfirm(const char* lowerTrimmed) {
  if (!lowerTrimmed) return false;
  const char* want = "maintenance confirm";
  for (; *want; ++want, ++lowerTrimmed) if (*lowerTrimmed != *want) return false;
  return *lowerTrimmed == '\0';
}

// While in maintenance the USB console accepts only these (no QR/session input).
inline bool allowedDuringMaintenance(const char* lowerTrimmed) {
  if (!lowerTrimmed) return false;
  const char* ok[] = {"maintenance exit", "status", "help", "?"};
  for (const char* w : ok) {
    const char* a = w; const char* b = lowerTrimmed;
    while (*a && *a == *b) { ++a; ++b; }
    if (!*a && !*b) return true;
  }
  return false;
}

static const uint32_t MAINTENANCE_IDLE_TIMEOUT_MS = 10UL * 60UL * 1000UL;

// millis() wrap-safe.
inline bool maintenanceTimedOut(uint32_t lastActivityMs, uint32_t nowMs) {
  return (uint32_t)(nowMs - lastActivityMs) >= MAINTENANCE_IDLE_TIMEOUT_MS;
}

}  // namespace mfmt
