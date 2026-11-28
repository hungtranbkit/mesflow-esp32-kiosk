#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace mfusb {
inline bool isHex64(const char* value) {
  if (!value || strlen(value) != 64) return false;
  for (const char* p = value; *p; ++p)
    if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))) return false;
  return true;
}

inline bool canonicalMac(const char* mac, char out[18]) {
  if (!mac || strlen(mac) != 17) return false;
  for (size_t i = 0; i < 17; ++i) {
    if (i % 3 == 2) {
      if (mac[i] != ':' && mac[i] != '-') return false;
      out[i] = ':';
    } else {
      char c = mac[i];
      if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 'a' + 'A');
      if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
      out[i] = c;
    }
  }
  out[17] = '\0';
  return true;
}

inline bool deriveDeviceId(const char* mac, char out[32]) {
  char canonical[18];
  if (!canonicalMac(mac, canonical)) return false;
  char compact[13];
  size_t j = 0;
  for (size_t i = 0; i < 17; ++i) if (canonical[i] != ':') compact[j++] = canonical[i];
  compact[j] = '\0';
  return snprintf(out, 32, "ESP32-KIOSK-%c%c%c%c", compact[8], compact[9], compact[10], compact[11]) > 0;
}

inline bool mayProvision(bool maintenance, bool pending, bool bound, bool idle,
                         bool pendingTransaction, bool queuedEvents) {
  return maintenance && pending && !bound && idle && !pendingTransaction && !queuedEvents;
}

inline bool mayAutoConfigure(bool maintenance, bool bound, bool credentialPresent,
                             bool idle, bool pendingTransaction, bool queuedEvents,
                             bool otaInProgress, bool identityMatches,
                             bool existingIdIsSafe) {
  return maintenance && !bound && !credentialPresent && idle && !pendingTransaction &&
         !queuedEvents && !otaInProgress && identityMatches && existingIdIsSafe;
}

inline bool configValueUnchangedOrFactoryDefault(const char* current, const char* requested,
                                                  const char* factoryDefault) {
  return current && requested && factoryDefault &&
         (strcmp(current, requested) == 0 || strcmp(current, factoryDefault) == 0);
}

inline bool factoryOrSameId(const char* current, const char* desired) {
  return current && desired &&
         (strcmp(current, "ESP32-KIOSK-001") == 0 || strcmp(current, desired) == 0);
}
}  // namespace mfusb
