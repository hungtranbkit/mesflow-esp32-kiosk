#include "scanner_qr.h"

#include <cassert>
#include <string>

int main() {
  const std::string accented =
      u8"WF|OP|6391 - NEWARK SIDE CHAIR ( GHẾ KHÔNG TAY )-KM-367170-204-03-OP01";
  std::string decoded;
  for (unsigned char byte : accented) {
    if (isScannerQrDataByte(byte)) decoded.push_back(static_cast<char>(byte));
  }
  assert(decoded == accented);
  assert(isMesflowQrPayload(decoded.c_str()));
  assert(isMesflowOperationQr(decoded.c_str()));

  assert(isMesflowQrPayload("WF|OPID|83001"));
  assert(isMesflowOperationQr("WF|OPID|83001"));
  assert(!isMesflowQrPayload("not-a-MESFlow-QR"));
  assert(!isMesflowOperationQr("WF|EMP|NV001"));
  assert(!isScannerQrDataByte('\r'));
  assert(!isScannerQrDataByte('\n'));
  assert(!isScannerQrDataByte(0x7f));
  return 0;
}
