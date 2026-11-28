#include <cassert>
#include <cstdio>
#include <string>
#include "mesflow_scanner_qr_policy.h"

static std::string decode(const std::string& frame) {
  std::string out;
  for (unsigned char byte : frame) {
    if (mfqr::isDataByte(byte)) out += static_cast<char>(byte);
  }
  return out;
}

int main() {
  const std::string qr = "WF|OP|6391 - GHẾ KHÔNG TAY";
  assert(decode(qr + "\r\n") == qr);
  assert(decode("WF|OPID|83001\r") == "WF|OPID|83001");
  assert(decode("WF|EMP|NV001") == "WF|EMP|NV001");
  assert(decode(std::string("A\0B\t\x7f", 5)) == "AB");
  assert(mfqr::isDataByte(0xc3));
  assert(mfqr::isDataByte(0xa9));
  assert(!mfqr::isAsciiPrintable(0xc3));
  assert(mfqr::isAsciiPrintable(0x7e));
  assert(!mfqr::isAsciiPrintable(0x7f));
  assert(mfqr::isOperationQr("WF|OP|6391 - GHẾ KHÔNG TAY"));
  assert(mfqr::isOperationQr("WF|OPID|83001"));
  assert(!mfqr::isOperationQr("WF|EMP|NV001"));
  assert(!mfqr::isOperationQr("not-a-MESFlow-QR"));
  assert(mfqr::isMesflowQr("WF|EMP|NV001"));
  assert(mfqr::isMesflowQr("WF|OPID|83001"));
  assert(mfqr::isRecentRepeat("WF|OP|6391 - GHẾ KHÔNG TAY", "WF|OP|6391 - GHẾ KHÔNG TAY", 0));
  assert(mfqr::isRecentRepeat("WF|EMP|NV001", "WF|EMP|NV001", 2999));
  assert(!mfqr::isRecentRepeat("WF|EMP|NV001", "WF|EMP|NV001", 3000));
  assert(!mfqr::isRecentRepeat("WF|EMP|NV002", "WF|EMP|NV001", 100));
  assert(!mfqr::isRecentRepeat("not-a-QR", "not-a-QR", 100));
  assert(!mfqr::isRecentRepeat("WF|OPID|1", "WF|OPID|1", 100, 0));
  std::puts("scanner QR UTF-8 policy: PASS");
}
