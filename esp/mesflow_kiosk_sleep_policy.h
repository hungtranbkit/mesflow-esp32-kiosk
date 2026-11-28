#pragma once
#include <stdint.h>
namespace mfks {
struct Config { bool enabled; uint16_t start; uint16_t end; };
inline uint32_t waitSeconds(const Config& c, bool valid, uint32_t daySeconds) {
  if (!valid || !c.enabled || c.start >= 1440 || c.end >= 1440 || c.start == c.end || daySeconds >= 86400) return 0;
  const uint32_t start = uint32_t(c.start)*60, end = uint32_t(c.end)*60;
  const bool inside = start < end ? daySeconds >= start && daySeconds < end : daySeconds >= start || daySeconds < end;
  if (!inside) return 0;
  return daySeconds < end ? end-daySeconds : 86400-daySeconds+end;
}
inline bool maySleep(bool enabled, bool clockValid, bool busy, bool overrideActive) {
  return enabled && clockValid && !busy && !overrideActive;
}
}
