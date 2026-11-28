#pragma once
#include <cstdint>

namespace mfqr {
// A UART QR frame carries UTF-8 bytes, not merely printable ASCII.
// Strip ASCII control characters and DEL; preserve UTF-8 payload bytes.
constexpr bool isDataByte(std::uint8_t value) {
  return value >= 0x20 && value != 0x7f;
}

// Keep the existing printable-ASCII ratio for wrong-baud detection on
// rejected frames; accepting UTF-8 bytes must not disable baud recovery.
constexpr bool isAsciiPrintable(std::uint8_t value) {
  return value >= 0x20 && value <= 0x7e;
}
}
