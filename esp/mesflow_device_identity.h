#pragma once
// Display-only identity rules, independent of Arduino for host regression tests.
// Canonical names come from authenticated MESFlow responses. UUID/auth/station
// are never modified by these helpers.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace mfidentity {

inline bool whitespace(uint32_t cp) {
  return cp == 0x20 || cp == 0x85 || cp == 0xA0 || cp == 0x1680 ||
         (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 ||
         cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

// Limit is encoded bytes (NVS char[64]), not Unicode character count.
// Reject truncated/overlong UTF-8, surrogates, NUL and C0/DEL controls.
inline bool validName(const char* name, size_t bytes) {
  if (!name || bytes == 0 || bytes > 63) return false;
  bool visible = false;
  for (size_t i = 0; i < bytes;) {
    const uint8_t lead = static_cast<uint8_t>(name[i++]);
    uint32_t cp = lead, minimum = 0;
    size_t more = 0;
    if (lead < 0x80) {}
    else if (lead >= 0xC2 && lead <= 0xDF) { cp = lead & 0x1F; more = 1; minimum = 0x80; }
    else if (lead >= 0xE0 && lead <= 0xEF) { cp = lead & 0x0F; more = 2; minimum = 0x800; }
    else if (lead >= 0xF0 && lead <= 0xF4) { cp = lead & 0x07; more = 3; minimum = 0x10000; }
    else return false;
    if (i + more > bytes) return false;
    for (size_t j = 0; j < more; ++j) {
      const uint8_t byte = static_cast<uint8_t>(name[i++]);
      if ((byte & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (byte & 0x3F);
    }
    if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) ||
        cp < 0x20 || cp == 0x7F) return false;
    if (!whitespace(cp)) visible = true;
  }
  return visible;
}

// Persist before publishing the new name; failed saves remain retryable.
// Repeated heartbeats with the same name do not write flash or repaint.
template<class Persist>
inline bool adoptName(const char* name, size_t bytes, char* current,
                      size_t capacity, Persist persist) {
  if (!current || bytes >= capacity || !validName(name, bytes)) return false;
  if (strncmp(current, name, bytes) == 0 && current[bytes] == '\0') return false;
  if (!persist(name)) return false;
  memcpy(current, name, bytes);
  current[bytes] = '\0';
  return true;
}

inline bool shortDeviceId(const char* uuid, char out[9]) {
  out[0] = '\0';
  if (!uuid) return false;
  char hex[32];
  size_t count = 0;
  for (const char* p = uuid; *p; ++p) {
    if (*p == '-') continue;
    char c = *p;
    if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 'a' + 'A');
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) || count == sizeof(hex)) return false;
    hex[count++] = c;
  }
  if (count != sizeof(hex)) return false;
  memcpy(out, hex + 24, 8);
  out[8] = '\0';
  return true;
}

enum class Refresh : uint8_t { NONE, HEADER, APPROVAL };

class DisplayChanges {
 public:
  void approved() { approval_ = true; }
  void renamed() { name_ = true; }
  Refresh needed(bool idle, bool busy) const {
    if (!idle || busy) return Refresh::NONE;
    return approval_ ? Refresh::APPROVAL : (name_ ? Refresh::HEADER : Refresh::NONE);
  }
  // Called by the real header renderer. A natural screen transition also
  // consumes the change, so a deferred rename never redraws a worker screen.
  void drawn() { approval_ = name_ = false; }
 private:
  bool approval_ = false;
  bool name_ = false;
};

}  // namespace mfidentity
