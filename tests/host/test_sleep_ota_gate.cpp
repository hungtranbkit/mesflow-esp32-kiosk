#include <cassert>
#include <cstdio>
#include <mutex>
#include <thread>
#include "mesflow_sleep_ota_gate.h"
int main() {
 mfks::OtaSleepGate g{};
 assert(g.beginOta()); assert(!g.beginSleep()); assert(!g.beginOta());
 g.endOta(); assert(g.beginSleep()); assert(!g.beginOta()); assert(!g.beginSleep());
 g.endSleep(); assert(g.beginOta()); g.endOta(); assert(g.beginSleep()); g.endSleep();
 std::mutex lock; unsigned granted=0;
 for (int i=0; i<1000; ++i) {
  g={}; granted=0;
  std::thread a([&]{std::lock_guard<std::mutex> guard(lock); if(g.beginOta()) ++granted;});
  std::thread b([&]{std::lock_guard<std::mutex> guard(lock); if(g.beginSleep()) ++granted;});
  a.join(); b.join(); assert(granted==1 && (g.ota != g.sleeping));
 }
 puts("OTA/sleep gate: interleavings and 1000 threaded races passed");
}
