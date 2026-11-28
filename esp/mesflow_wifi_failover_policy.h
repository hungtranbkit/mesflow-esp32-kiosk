#pragma once

#include <stdint.h>

namespace mfwifi {

constexpr int32_t GOOD_RSSI_DBM = -70;

inline int8_t chooseKnownNetwork(const bool configured[3], const int32_t strongestRssi[3]) {
  for (int8_t i = 0; i < 3; ++i)
    if (configured[i] && strongestRssi[i] >= GOOD_RSSI_DBM) return i;
  int8_t best = -1;
  for (int8_t i = 0; i < 3; ++i)
    if (configured[i] && strongestRssi[i] > -127 &&
        (best < 0 || strongestRssi[i] > strongestRssi[best])) best = i;
  return best;
}

inline int8_t nextConfiguredNetwork(const bool configured[3], int8_t current) {
  if (current < 0 || current > 2) current = 0;
  for (int8_t step = 1; step <= 3; ++step) {
    const int8_t candidate = static_cast<int8_t>((current + step) % 3);
    if (configured[candidate]) return candidate;
  }
  return -1;
}

}  // namespace mfwifi
