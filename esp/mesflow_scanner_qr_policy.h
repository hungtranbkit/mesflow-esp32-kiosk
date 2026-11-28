#pragma once
#include <cstdint>
#include <cstring>

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

constexpr bool isOperationQr(const char* value) {
  return value && (std::strncmp(value, "WF|OP|", 6) == 0 ||
                   std::strncmp(value, "WF|OPID|", 8) == 0);
}
}
