#pragma once

#include <cstdint>
#include <cstring>

// The serial QR reader sends decoded UTF-8 bytes. Preserve those bytes for
// QR payloads; strip only ASCII control bytes that delimit/corrupt a frame.
inline bool isScannerQrDataByte(uint8_t value) {
  return value >= 0x20 && value != 0x7f;
}

inline bool isMesflowQrPayload(const char* value) {
  return value && (std::strncmp(value, "WF|EMP|", 7) == 0 ||
                   std::strncmp(value, "WF|OP|", 6) == 0 ||
                   std::strncmp(value, "WF|OPID|", 8) == 0);
}

inline bool isMesflowOperationQr(const char* value) {
  return value && (std::strncmp(value, "WF|OP|", 6) == 0 ||
                   std::strncmp(value, "WF|OPID|", 8) == 0);
}
