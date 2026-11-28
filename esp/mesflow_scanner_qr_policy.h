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

constexpr bool isMesflowQr(const char* value) {
  return isOperationQr(value) || (value && std::strncmp(value, "WF|EMP|", 7) == 0);
}

// Suppress only exact repeats of a valid worker/operation QR during the short
// operator re-scan window. Unsigned subtraction is millis()-wrap safe.
constexpr bool isRecentRepeat(const char* current, const char* previous,
                              std::uint32_t elapsedMs,
                              std::uint32_t windowMs = 3000) {
  return current && previous && isMesflowQr(current) &&
         std::strcmp(current, previous) == 0 && elapsedMs < windowMs;
}
}
