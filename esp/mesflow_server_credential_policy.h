#pragma once
// Pure server-credential policy (no Arduino dependency), host tested in
// tests/host/test_server_credential_policy.cpp.
//
// v5.6.5 bench finding (40:D0, 2026-11-12): changing the MESFlow URL in
// maintenance kept rt.bound=true and the PRODUCTION kiosk token, so the
// staging server answered 403 to every heartbeat and bind could never
// start enrollment. The phone/setup-portal path had the opposite bug: any
// server change silently deleted the token. A kiosk token is only valid for
// the server that issued it, so credentials are now scoped per server:
//  * the current token is remembered together with the server it belongs to
//    (token_srv); a token for another server never makes the kiosk "bound";
//  * changing server STASHES the current credential in one spare slot (alt)
//    instead of deleting it; switching back RESTORES it (no re-enrollment,
//    no admin action on production);
//  * a third server while the spare slot still holds another server's
//    credential is REFUSED (never silently drop a credential);
//  * the server cannot change while a pending transaction or unsent offline
//    events exist: they belong to the old server.
#include <stddef.h>
#include <stdint.h>

namespace mfcred {

// Compare server URLs ignoring ASCII case and trailing '/'.
inline bool sameServer(const char* a, const char* b) {
  if (!a || !b) return false;
  size_t la = 0, lb = 0;
  while (a[la]) ++la;
  while (b[lb]) ++lb;
  while (la && a[la - 1] == '/') --la;
  while (lb && b[lb - 1] == '/') --lb;
  if (la != lb || la == 0) return false;
  for (size_t i = 0; i < la; ++i) {
    char x = a[i], y = b[i];
    if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
    if (x != y) return false;
  }
  return true;
}

// A stored token makes the kiosk bound only for the server that issued it.
// Legacy NVS without token_srv: the token was issued for the configured server.
inline bool tokenUsableFor(bool tokenPresent, const char* tokenServer, const char* currentServer) {
  if (!tokenPresent) return false;
  if (!tokenServer || !tokenServer[0]) return true;  // pre-5.6.6 NVS
  return sameServer(tokenServer, currentServer);
}

enum class Switch : uint8_t {
  NO_CHANGE = 0,          // same server: keep everything
  STASH_AND_UNBIND,       // current credential -> spare slot; unbound on new server
  RESTORE_FROM_SPARE,     // spare slot belongs to new server: swap back, bound again
  CHANGE_UNBOUND,         // nothing to keep (no current token): just change server
  REFUSE_SPARE_OCCUPIED,  // spare slot holds a 3rd server's credential
  REFUSE_QUEUE_BUSY,      // pending transaction / unsent offline events
};

inline Switch decideSwitch(const char* oldServer, const char* newServer, bool curTokenPresent,
                           const char* spareServer, bool spareTokenPresent, bool queueBusy) {
  if (sameServer(oldServer, newServer)) return Switch::NO_CHANGE;
  if (queueBusy) return Switch::REFUSE_QUEUE_BUSY;
  if (spareTokenPresent && sameServer(spareServer, newServer)) return Switch::RESTORE_FROM_SPARE;
  if (!curTokenPresent) return Switch::CHANGE_UNBOUND;
  if (spareTokenPresent && !sameServer(spareServer, oldServer)) return Switch::REFUSE_SPARE_OCCUPIED;
  return Switch::STASH_AND_UNBIND;
}

inline const char* switchName(Switch s) {
  switch (s) {
    case Switch::NO_CHANGE: return "NO_CHANGE";
    case Switch::STASH_AND_UNBIND: return "STASH_AND_UNBIND";
    case Switch::RESTORE_FROM_SPARE: return "RESTORE_FROM_SPARE";
    case Switch::CHANGE_UNBOUND: return "CHANGE_UNBOUND";
    case Switch::REFUSE_SPARE_OCCUPIED: return "REFUSE_SPARE_OCCUPIED";
    case Switch::REFUSE_QUEUE_BUSY: return "REFUSE_QUEUE_BUSY";
  }
  return "UNKNOWN";
}

inline bool isRefusal(Switch s) { return s == Switch::REFUSE_SPARE_OCCUPIED || s == Switch::REFUSE_QUEUE_BUSY; }

// 6-char enrollment code = first 6 hex chars (upper case) of sha256(device_secret),
// identical to MESFlow KioskRepository.enrollment_code(). out must hold 7 bytes.
inline void enrollmentCodeFromDigest(const uint8_t digest[32], char out[7]) {
  static const char hex[] = "0123456789ABCDEF";
  for (int i = 0; i < 3; ++i) { out[2 * i] = hex[digest[i] >> 4]; out[2 * i + 1] = hex[digest[i] & 0x0F]; }
  out[6] = '\0';
}

}  // namespace mfcred
