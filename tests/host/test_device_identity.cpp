#include "mesflow_device_identity.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

int main() {
  char id[9];
  assert(mfidentity::shortDeviceId("0f2d2ad3-06d7-417f-a1e7-da3d2457db39", id));
  assert(std::strcmp(id, "2457DB39") == 0);
  assert(mfidentity::shortDeviceId("fc0c33a9-2f76-424f-a256-217699b57712", id));
  assert(std::strcmp(id, "99B57712") == 0);
  assert(!mfidentity::shortDeviceId("ESP32-KIOSK-001", id) && !id[0]);
  assert(!mfidentity::shortDeviceId("74C150", id) && !id[0]);

  const char* accepted[] = {"Máy 1", "Máy 2", "Chuyền may số 3", "A"};
  for (const char* text : accepted) assert(mfidentity::validName(text, std::strlen(text)));
  const char* rejected[] = {"", " \xC2\xA0\xE3\x80\x80", "A\nB", "A\x7f", "\xC0\xAF",
                            "\xE0\x80\x80", "\xED\xA0\x80", "\xF4\x90\x80\x80",
                            "\xE1\xBA", "\x80"};
  for (const char* text : rejected) assert(!mfidentity::validName(text, std::strlen(text)));
  assert(!mfidentity::validName("A\0B", 3));
  assert(!mfidentity::validName(nullptr, 3));
  std::string limit(63, 'A');
  assert(mfidentity::validName(limit.c_str(), limit.size()));
  limit.push_back('A');
  assert(!mfidentity::validName(limit.c_str(), limit.size()));
  std::string multibyte;
  for (int i = 0; i < 21; ++i) multibyte += "ế";  // exactly 63 encoded bytes
  assert(mfidentity::validName(multibyte.c_str(), multibyte.size()));
  multibyte += "A";
  assert(!mfidentity::validName(multibyte.c_str(), multibyte.size()));

  char name[64] = "Máy 1";
  int writes = 0;
  bool storageHealthy = false;
  auto persist = [&](const char* text) {
    ++writes;
    assert(std::strcmp(text, "Máy 2") == 0);
    assert(std::strcmp(name, "Máy 1") == 0);  // NVS precedes RAM/display
    return storageHealthy;
  };
  assert(!mfidentity::adoptName("Máy 2", std::strlen("Máy 2"), name, sizeof(name), persist));
  assert(writes == 1 && std::strcmp(name, "Máy 1") == 0);
  storageHealthy = true;
  assert(mfidentity::adoptName("Máy 2", std::strlen("Máy 2"), name, sizeof(name), persist));
  assert(writes == 2 && std::strcmp(name, "Máy 2") == 0);
  assert(!mfidentity::adoptName("Máy 2", std::strlen("Máy 2"), name, sizeof(name), persist));
  assert(!mfidentity::adoptName("", 0, name, sizeof(name), persist));
  assert(!mfidentity::adoptName(limit.c_str(), limit.size(), name, sizeof(name), persist));
  assert(writes == 2);  // no repeated flash writes or invalid-data writes

  mfidentity::DisplayChanges changes;
  changes.renamed();
  assert(changes.needed(false, false) == mfidentity::Refresh::NONE);
  assert(changes.needed(true, true) == mfidentity::Refresh::NONE);
  assert(changes.needed(true, false) == mfidentity::Refresh::HEADER);
  changes.approved();
  assert(changes.needed(true, false) == mfidentity::Refresh::APPROVAL);
  changes.drawn();
  assert(changes.needed(true, false) == mfidentity::Refresh::NONE);
  puts("PASS device identity: UUID mapping, UTF-8 boundaries, durable rename, deferred display");
}
