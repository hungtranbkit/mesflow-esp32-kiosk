#pragma once
namespace mfks {
// Caller must hold the same cross-core lock for every operation.
struct OtaSleepGate {
  bool ota = false;
  bool sleeping = false;
  bool setup = false;
  bool beginOta() { if (ota || sleeping || setup) return false; ota=true; return true; }
  void endOta() { ota=false; }
  bool beginSleep() { if (ota || sleeping || setup) return false; sleeping=true; return true; }
  void endSleep() { sleeping=false; }
  bool beginSetup() { if (ota || sleeping || setup) return false; setup=true; return true; }
  void endSetup() { setup=false; }
};
}
