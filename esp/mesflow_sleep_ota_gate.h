#pragma once
namespace mfks {
// Caller must hold the same cross-core lock for every operation.
struct OtaSleepGate {
  bool ota = false;
  bool sleeping = false;
  bool beginOta() { if (ota || sleeping) return false; ota=true; return true; }
  void endOta() { ota=false; }
  bool beginSleep() { if (ota || sleeping) return false; sleeping=true; return true; }
  void endSleep() { sleeping=false; }
};
}
