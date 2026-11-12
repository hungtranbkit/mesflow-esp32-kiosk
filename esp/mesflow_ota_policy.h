#pragma once
// Pure OTA policy helpers (no Arduino/IDF dependency) so they can be unit
// tested on the host: tests/host/test_ota_policy.cpp.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

namespace mfota {

// The compiled default of firmware <= 5.6.2. Kept ONLY to recognise legacy
// NVS values; it is never a default for a new install any more.
static const char LEGACY_DEFAULT_AGENT[] = "https://deploy.mesflow.net/agent";

// Numeric tuple compare of "5.6.10-X" style versions: -1 / 0 / 1.
inline int compareVersions(const char* a, const char* b) {
  if (!a) a = "";
  if (!b) b = "";
  while (*a || *b) {
    while (*a && !isdigit((unsigned char)*a)) a++;
    while (*b && !isdigit((unsigned char)*b)) b++;
    if (!*a && !*b) return 0;
    unsigned long x = 0, y = 0;
    while (isdigit((unsigned char)*a)) x = x * 10 + (unsigned long)(*a++ - '0');
    while (isdigit((unsigned char)*b)) y = y * 10 + (unsigned long)(*b++ - '0');
    if (x != y) return x < y ? -1 : 1;
  }
  return 0;
}

// Copies "scheme://host[:port]" of url into out (lower-case). False if malformed.
inline bool originOf(const char* url, char* out, size_t cap) {
  if (!url || !out || cap < 2) return false;
  const char* p = strstr(url, "://");
  if (!p || p == url) return false;
  const char* host = p + 3;
  const char* end = host;
  while (*end && *end != '/' && *end != '?' && *end != '#') end++;
  if (end == host) return false;
  for (const char* q = host; q < end; q++) if (*q == '@') return false;  // no userinfo
  size_t n = (size_t)(end - url);
  if (n + 1 > cap) return false;
  for (size_t i = 0; i < n; i++) out[i] = (char)tolower((unsigned char)url[i]);
  out[n] = '\0';
  return true;
}

// Download URL must be HTTPS and on the exact origin of the Agent we polled,
// otherwise the device would attach its X-Kiosk-Token to a foreign host.
inline bool sameHttpsOrigin(const char* agentBase, const char* url) {
  char a[200], b[200];
  if (!originOf(agentBase, a, sizeof(a)) || !originOf(url, b, sizeof(b))) return false;
  return strncmp(b, "https://", 8) == 0 && strcmp(a, b) == 0;
}

inline bool hostEndsWith(const char* url, const char* suffix) {
  char o[200];
  if (!originOf(url, o, sizeof(o))) return false;
  const char* host = strstr(o, "://") + 3;
  char h[200]; size_t i = 0;
  while (host[i] && host[i] != ':' && i + 1 < sizeof(h)) { h[i] = host[i]; i++; }
  h[i] = '\0';
  size_t hl = strlen(h), sl = strlen(suffix);
  if (hl < sl) return false;
  if (strcmp(h + hl - sl, suffix) != 0) return false;
  return hl == sl || h[hl - sl - 1] == '.';
}

enum class AgentSource : uint8_t { NONE = 0, LEGACY = 1, MESFLOW = 2, MANUAL = 3 };

// Effective OTA Agent: there is no compiled fallback, and SERVER_BASE is never
// assumed to host Agent routes. A legacy NVS value equal to the old shared
// default is only used while the kiosk's own MESFlow is a mesflow.net host
// (the original owner); for any other customer it waits until its MESFlow
// advertises its own Agent, so its device token never leaves its instance.
inline const char* effectiveAgentBase(const char* agent, AgentSource src, const char* serverBase) {
  if (!agent || strncmp(agent, "https://", 8) != 0 || strlen(agent) < 12) return "";
  if (src == AgentSource::LEGACY && strcmp(agent, LEGACY_DEFAULT_AGENT) == 0 && !hostEndsWith(serverBase, "mesflow.net")) return "";
  return agent;
}

// esp_image_header_t (24 B) + first segment header (8 B) + esp_app_desc_t.
// Returns nullptr when acceptable, else an error code for the OTA event.
inline const char* validateAppImageHead(const uint8_t* buf, size_t len, uint16_t expectedChipId) {
  if (!buf || len < 36) return "OTA_IMAGE_INVALID";
  if (buf[0] != 0xE9) return "OTA_IMAGE_INVALID";
  uint16_t chip = (uint16_t)(buf[12] | (buf[13] << 8));
  if (chip != expectedChipId) return "OTA_IMAGE_CHIP_MISMATCH";
  uint32_t magic = (uint32_t)buf[32] | ((uint32_t)buf[33] << 8) | ((uint32_t)buf[34] << 16) | ((uint32_t)buf[35] << 24);
  if (magic != 0xABCD5432UL) return "OTA_IMAGE_NOT_APP";
  return nullptr;
}

struct SafePointInputs {
  bool linkReady;            // Wi-Fi + bound + MESFlow online
  bool uiReady;              // uiState == READY (no scan/confirmation flow on screen)
  bool pendingTransaction;   // durable START/FINISH journal not yet ACKed
  uint16_t offlineEvents;    // queued offline events not yet ACKed
  uint16_t actionQueue;      // queued action events
  bool maintenanceOrCalibration;
  bool offlineMode;
  bool longTermSession;      // worker/operation selected or server-confirmed open session
  bool force;                // offer comes from a forced batch
  uint32_t waitingMs;        // how long this offer has been waiting for a safe point
};

enum class SafePoint : uint8_t { SAFE, WAIT_TRANSACTION, WAIT_BUSY, WAIT_SESSION, BLOCKED_TRANSACTION };

static const uint32_t BLOCKED_TRANSACTION_AFTER_MS = 30UL * 60UL * 1000UL;

// Work sessions are durable on the server and restored after reboot, so a
// FORCED update does not wait for them. In-flight data on the ESP (journal,
// queues, a scan flow on screen) always must finish/ACK first; if it cannot
// for 30 min the reason becomes BLOCKED_TRANSACTION (never skipped/success).
inline SafePoint evaluateSafePoint(const SafePointInputs& in) {
  const bool txOpen = in.pendingTransaction || in.offlineEvents || in.actionQueue || in.offlineMode;
  if (!in.linkReady || txOpen) return in.waitingMs >= BLOCKED_TRANSACTION_AFTER_MS ? SafePoint::BLOCKED_TRANSACTION : SafePoint::WAIT_TRANSACTION;
  if (!in.uiReady || in.maintenanceOrCalibration) return SafePoint::WAIT_BUSY;
  if (in.longTermSession && !in.force) return SafePoint::WAIT_SESSION;
  return SafePoint::SAFE;
}

inline const char* safePointReason(SafePoint s) {
  switch (s) {
    case SafePoint::SAFE: return "";
    case SafePoint::WAIT_TRANSACTION: return "WAITING_TRANSACTION_SYNC";
    case SafePoint::WAIT_BUSY: return "DEVICE_BUSY";
    case SafePoint::WAIT_SESSION: return "WAITING_SESSION";
    case SafePoint::BLOCKED_TRANSACTION: return "BLOCKED_TRANSACTION";
  }
  return "DEVICE_BUSY";
}

}  // namespace mfota
