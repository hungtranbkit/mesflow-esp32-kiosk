// Host unit tests for esp/mesflow_server_credential_policy.h (pure logic, not a hardware test).
// Build+run: g++ -std=c++17 -Wall -Wextra -Werror -I esp tests/host/test_server_credential_policy.cpp -o /tmp/c && /tmp/c
#include <cstdio>
#include <cstring>
#include "mesflow_server_credential_policy.h"

using namespace mfcred;
static int fails = 0, total = 0;
#define CHECK(c) do { total++; if (!(c)) { fails++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static const char* PROD = "https://mesflow.net:443";
static const char* STG = "http://192.168.1.109:18080";
static const char* OTHER = "https://other.example";

int main() {
  CHECK(sameServer("https://mesflow.net:443/", "HTTPS://MESFLOW.NET:443"));
  CHECK(!sameServer(PROD, STG));
  CHECK(!sameServer("", ""));
  CHECK(!sameServer(nullptr, PROD));
  // 40:D0 regression: a production token must not make the kiosk "bound" on staging.
  CHECK(!tokenUsableFor(true, PROD, STG));
  CHECK(tokenUsableFor(true, PROD, PROD));
  CHECK(tokenUsableFor(true, "", PROD));          // pre-5.6.6 NVS keeps working
  CHECK(!tokenUsableFor(false, "", PROD));
  // prod -> staging: stash prod credential (not deleted), unbound on staging.
  CHECK(decideSwitch(PROD, STG, true, "", false, false) == Switch::STASH_AND_UNBIND);
  // staging (enrolled) -> prod: restore prod, staging credential goes to the spare slot.
  CHECK(decideSwitch(STG, PROD, true, PROD, true, false) == Switch::RESTORE_FROM_SPARE);
  // staging (never enrolled) -> prod: restore prod.
  CHECK(decideSwitch(STG, PROD, false, PROD, true, false) == Switch::RESTORE_FROM_SPARE);
  // no credential at all: just change.
  CHECK(decideSwitch(STG, OTHER, false, "", false, false) == Switch::CHANGE_UNBOUND);
  // third server while spare holds prod and current holds staging: refuse (no silent delete).
  CHECK(decideSwitch(STG, OTHER, true, PROD, true, false) == Switch::REFUSE_SPARE_OCCUPIED);
  CHECK(isRefusal(Switch::REFUSE_SPARE_OCCUPIED) && isRefusal(Switch::REFUSE_QUEUE_BUSY));
  // spare is a stale copy of the same server: overwrite with the newer one is fine.
  CHECK(decideSwitch(PROD, STG, true, PROD, true, false) == Switch::STASH_AND_UNBIND);
  // unsent journal/offline events belong to the old server.
  CHECK(decideSwitch(PROD, STG, true, "", false, true) == Switch::REFUSE_QUEUE_BUSY);
  CHECK(decideSwitch(STG, PROD, true, PROD, true, true) == Switch::REFUSE_QUEUE_BUSY);
  // same server (only trailing slash/case): nothing changes, even with a busy queue.
  CHECK(decideSwitch(PROD, "https://MESFLOW.net:443/", true, "", false, true) == Switch::NO_CHANGE);
  // Enrollment code = sha256 prefix, upper hex (same as MESFlow enrollment_code()).
  const uint8_t d[32] = {0xab, 0x0c, 0x9f};
  char code[7]; enrollmentCodeFromDigest(d, code);
  CHECK(std::strcmp(code, "AB0C9F") == 0);
  std::printf("%d/%d checks passed\n", total - fails, total);
  return fails ? 1 : 0;
}
