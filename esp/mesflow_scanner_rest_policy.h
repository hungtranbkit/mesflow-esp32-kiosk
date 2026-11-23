#pragma once
#include <stdint.h>
namespace mfscan {
struct Config { bool enabled; uint16_t start; uint16_t end; };
inline bool suppressed(const Config& c, bool clockValid, uint16_t minute) {
  if (!c.enabled || !clockValid || c.start >= 1440 || c.end >= 1440 || minute >= 1440 || c.start == c.end) return false;
  return c.start < c.end ? minute >= c.start && minute < c.end : minute >= c.start || minute < c.end;
}
inline bool elapsed(uint32_t previous, uint32_t now, uint32_t interval) { return uint32_t(now - previous) >= interval; }
}
