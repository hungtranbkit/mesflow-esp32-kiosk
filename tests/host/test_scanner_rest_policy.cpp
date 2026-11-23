#include <cassert>
#include <cstdio>
#include "mesflow_scanner_rest_policy.h"
int main() {
  using namespace mfscan;
  const Config c{true, 22*60, 6*60};
  assert(!suppressed(c, true, 21*60+59));
  assert(suppressed(c, true, 22*60));
  assert(suppressed(c, true, 23*60+59));
  assert(suppressed(c, true, 0));
  assert(suppressed(c, true, 5*60+59));
  assert(!suppressed(c, true, 6*60));
  assert(!suppressed(c, false, 23*60));
  assert(!suppressed(Config{false, 1320, 360}, true, 1380));
  assert(!suppressed(Config{true, 360, 360}, true, 360));
  assert(suppressed(Config{true, 480, 1020}, true, 600));
  assert(!suppressed(Config{true, 480, 1020}, true, 1020));
  assert(!suppressed(Config{true, 1440, 360}, true, 1380));
  assert(suppressed(c, true, 1380)); // reboot recalculates from wall clock
  assert(!suppressed(c, true, 600)); // clock jump recalculates, no latch
  assert(elapsed(0xfffffff0u, 0x00000020u, 40u));
  assert(!elapsed(0xfffffff0u, 0x00000010u, 40u));
  std::puts("scanner rest policy: 16 assertions passed");
}
