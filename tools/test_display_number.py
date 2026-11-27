"""Exercise real numeric intake, NVS adapter and LCD header with hardware edges faked."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_enrollment_display import function

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "esp/mesflow_app.cpp"

PREFIX = r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <limits>
#include <vector>
#include "mesflow_device_identity.h"
enum class JsonKind { INTEGER, DECIMAL, BOOLEAN, STRING, NIL };
struct JsonVariantConst {
  JsonKind kind;
  int64_t number;
  JsonVariantConst(int64_t value) : kind(JsonKind::INTEGER), number(value) {}
  JsonVariantConst(JsonKind k, int64_t value) : kind(k), number(value) {}
  template<class T> bool is() const {
    if constexpr (std::is_same<T, bool>::value) return kind == JsonKind::BOOLEAN;
    if (kind != JsonKind::INTEGER) return false;
    if constexpr (std::is_unsigned<T>::value)
      return number >= 0 && static_cast<uint64_t>(number) <= std::numeric_limits<T>::max();
    return number >= std::numeric_limits<T>::lowest() && number <= std::numeric_limits<T>::max();
  }
  template<class T> T as() const { return static_cast<T>(number); }
};
std::vector<uint8_t> stored;
bool openOk = true, writeOk = true, readOk = true, corruptRead = false;
unsigned writes = 0;
struct Preferences {
  bool begin(const char* ns, bool) { assert(std::strcmp(ns, "mesflow_cfg") == 0); return openOk; }
  size_t putBytes(const char* key, const void* data, size_t size) {
    assert(std::strcmp(key, "display_num") == 0);
    ++writes;
    if (!writeOk) return 0;
    const auto* bytes = static_cast<const uint8_t*>(data);
    stored.assign(bytes, bytes + size);
    return size;
  }
  size_t getBytesLength(const char* key) {
    assert(std::strcmp(key, "display_num") == 0);
    return stored.size();
  }
  size_t getBytes(const char* key, void* data, size_t size) {
    assert(std::strcmp(key, "display_num") == 0);
    if (!readOk || size != stored.size()) return 0;
    std::memcpy(data, stored.data(), size);
    if (corruptRead && size) static_cast<uint8_t*>(data)[0] ^= 0xff;
    return size;
  }
  void end() {}
};
char DEVICE_NAME[64] = "Máy 2";
char DEVICE_UUID[40] = "0f2d2ad3-06d7-417f-a1e7-da3d2457db39";
char SERVER_BASE[192] = "https://mesflow.net:443";
mfidentity::DisplayChanges identityDisplayChanges;
constexpr int C_OK = 1, C_BG = 0, C_TEXT = 2, C_MUTED = 3, C_PANEL_2 = 4;
constexpr int SW = 240, HEADER_H = 36, CLOCK_X = 150, FONT_SECTION = 16, FONT_HEADER = 12;
enum class TextAlign { LEFT };
struct Tft {
  void fillRect(int, int, int, int, int) {}
  void drawFastHLine(int, int, int, int) {}
} tft;
std::string lcdLabel;
void drawTextBox(const char*, int, int, int, int, int, int, int, TextAlign, int, bool) {}
void printText(int, int, const char* text, int, int) { lcdLabel = text; }
void serviceNetworkIndicator(bool) {}
"""

MAIN = r"""
int main() {
  loadDisplayNumberConfig();
  drawIndustrialHeader();
  assert(lcdLabel == "ID --");
  assert(adoptServerDisplayNumber(JsonVariantConst(2)));
  drawIndustrialHeader();
  assert(lcdLabel == "ID 02" && writes == 1);

  // A reboot loads the same single record without another write.
  displayNumberRecord = {};
  loadDisplayNumberConfig();
  drawIndustrialHeader();
  assert(lcdLabel == "ID 02" && writes == 1);
  std::strcpy(SERVER_BASE, "HTTPS://MESFLOW.NET:443///");
  assert(!adoptServerDisplayNumber(JsonVariantConst(2)));
  assert(writes == 1);
  drawIndustrialHeader();
  assert(lcdLabel == "ID 02");

  // Missing/invalid fields do not replace a valid same-server number.
  const JsonVariantConst rejected[] = {
    {JsonKind::NIL, 0}, {JsonKind::BOOLEAN, 1}, {JsonKind::STRING, 2},
    {JsonKind::DECIMAL, 2}, {0}, {-1}, {2147483648LL}, {INT64_MAX}
  };
  for (const auto value : rejected) assert(!adoptServerDisplayNumber(value));
  drawIndustrialHeader();
  assert(lcdLabel == "ID 02" && writes == 1);

  // Readback, open and write failures never publish the candidate.
  openOk = false;
  assert(!adoptServerDisplayNumber(JsonVariantConst(3)));
  openOk = true; writeOk = false;
  assert(!adoptServerDisplayNumber(JsonVariantConst(3)));
  writeOk = true; readOk = false;
  assert(!adoptServerDisplayNumber(JsonVariantConst(3)));
  readOk = true; corruptRead = true;
  assert(!adoptServerDisplayNumber(JsonVariantConst(3)));
  drawIndustrialHeader();
  assert(lcdLabel == "ID 02");
  corruptRead = false;
  assert(adoptServerDisplayNumber(JsonVariantConst(3)));
  drawIndustrialHeader();
  assert(lcdLabel == "ID 03");

  // Server switch and full UUID mismatch hide the previous server's code.
  std::strcpy(SERVER_BASE, "https://staging.mesflow.net");
  drawIndustrialHeader();
  assert(lcdLabel == "ID --");
  assert(!adoptServerDisplayNumber({JsonKind::NIL, 0}));
  std::strcpy(SERVER_BASE, "https://mesflow.net:443/");
  drawIndustrialHeader();
  assert(lcdLabel == "ID 03");
  std::strcpy(DEVICE_UUID, "fc0c33a9-2f76-424f-a256-217699b57712");
  drawIndustrialHeader();
  assert(lcdLabel == "ID --");
  assert(adoptServerDisplayNumber(JsonVariantConst(1)));
  drawIndustrialHeader();
  assert(lcdLabel == "ID 01");
  std::strcpy(DEVICE_UUID, "0f2d2ad3-06d7-417f-a1e7-da3d2457db39");
  drawIndustrialHeader();
  assert(lcdLabel == "ID --");

  // Values above 99 retain every decimal digit.
  assert(adoptServerDisplayNumber(JsonVariantConst(100)));
  drawIndustrialHeader();
  assert(lcdLabel == "ID 100");
  assert(adoptServerDisplayNumber(JsonVariantConst(2147483647)));
  drawIndustrialHeader();
  assert(lcdLabel == "ID 2147483647");
  displayNumberRecord = {};
  loadDisplayNumberConfig();
  drawIndustrialHeader();
  assert(lcdLabel == "ID 2147483647");

  // Unknown/truncated records cannot become a displayed number after reboot.
  stored[0] ^= 0xff;
  loadDisplayNumberConfig();
  drawIndustrialHeader();
  assert(lcdLabel == "ID --");
  stored[0] ^= 0xff;
  stored.pop_back();
  loadDisplayNumberConfig();
  drawIndustrialHeader();
  assert(lcdLabel == "ID --");
  puts("PASS display number: strict intake, durable NVS/reboot, server+UUID scope, full decimal LCD");
}
"""


class DisplayNumberAdapterTest(unittest.TestCase):
    def test_real_intake_storage_and_lcd_number(self):
        source = SOURCE.read_text()
        header = function(source, "void drawIndustrialHeader(uint16_t statusColor = C_OK)")
        if "static bool adoptServerDisplayNumber(" not in source:
            # Older firmware ignores the new optional response field.
            code = PREFIX + header + r"""
int main() {
  drawIndustrialHeader();
  assert(lcdLabel == "ID 02");
}
"""
        else:
            code = PREFIX + "\nstatic mfidentity::DisplayNumberRecord displayNumberRecord;\n"
            for signature in (
                "static uint32_t effectiveDisplayNumber()",
                "static void loadDisplayNumberConfig()",
                "static bool adoptServerDisplayNumber(",
            ):
                code += function(source, signature) + "\n"
            code += header + MAIN
        with tempfile.TemporaryDirectory(prefix="esp-display-number-") as tmp:
            unit, exe = Path(tmp) / "main.cpp", Path(tmp) / "test"
            unit.write_text(code)
            built = subprocess.run(
                ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 "-Wno-unused-parameter", "-I", str(ROOT / "esp"), str(unit), "-o", str(exe)],
                capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

