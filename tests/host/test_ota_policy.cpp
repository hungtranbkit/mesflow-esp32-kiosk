// Host unit tests for esp/mesflow_ota_policy.h (pure logic, not a hardware test).
// Build+run: g++ -std=c++17 -Wall -Wextra -I esp tests/host/test_ota_policy.cpp -o /tmp/t && /tmp/t
#include <cstdio>
#include <cstdlib>
#include <string>
#include "mesflow_ota_policy.h"

using namespace mfota;
static int fails = 0, total = 0;
#define CHECK(c) do { total++; if (!(c)) { fails++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static void versions() {
  CHECK(compareVersions("5.6.3", "5.6.2") == 1);
  CHECK(compareVersions("5.6.2", "5.6.2") == 0);
  CHECK(compareVersions("5.6.2-KIMEX-QR-HOLD", "5.6.2") == 0);
  CHECK(compareVersions("5.6.10", "5.6.9") == 1);
  CHECK(compareVersions("5.5.12", "5.6.0") == -1);
}

static void origins() {
  const char* agent = "https://deploy.mesflow.net/agent";
  CHECK(sameHttpsOrigin(agent, "https://deploy.mesflow.net/agent/api/esp-ota/file/x?kiosk_id=k"));
  CHECK(sameHttpsOrigin(agent, "https://DEPLOY.mesflow.net/agent/api/x"));
  CHECK(!sameHttpsOrigin(agent, "https://evil.example/agent/api/x"));
  CHECK(!sameHttpsOrigin(agent, "http://deploy.mesflow.net/agent/api/x"));
  CHECK(!sameHttpsOrigin(agent, "https://deploy.mesflow.net:8443/agent/api/x"));
  CHECK(!sameHttpsOrigin(agent, "https://deploy.mesflow.net@evil.example/x"));
  CHECK(!sameHttpsOrigin(agent, "https://deploy.mesflow.net.evil.example/x"));
  CHECK(!sameHttpsOrigin(agent, "/relative"));
}

static void agentBase() {
  CHECK(std::string(effectiveAgentBase("", AgentSource::NONE, "http://mesflow.net")) == "");
  CHECK(std::string(effectiveAgentBase(LEGACY_DEFAULT_AGENT, AgentSource::LEGACY, "http://mesflow.net")) == LEGACY_DEFAULT_AGENT);
  CHECK(std::string(effectiveAgentBase(LEGACY_DEFAULT_AGENT, AgentSource::LEGACY, "https://app.mesflow.net")) == LEGACY_DEFAULT_AGENT);
  // another customer's kiosk carrying the old compiled default never polls it
  CHECK(std::string(effectiveAgentBase(LEGACY_DEFAULT_AGENT, AgentSource::LEGACY, "https://mes.customer.example")) == "");
  CHECK(std::string(effectiveAgentBase(LEGACY_DEFAULT_AGENT, AgentSource::LEGACY, "https://notmesflow.net")) == "");
  // advertised by the kiosk's own MESFlow over its authenticated channel
  CHECK(std::string(effectiveAgentBase("https://ota.customer.example/agent", AgentSource::MESFLOW, "https://mes.customer.example")) == "https://ota.customer.example/agent");
  CHECK(std::string(effectiveAgentBase("http://ota.customer.example/agent", AgentSource::MANUAL, "x")) == "");
}

static void image() {
  uint8_t img[64] = {0};
  img[0] = 0xE9; img[12] = 9; img[13] = 0;
  img[32] = 0x32; img[33] = 0x54; img[34] = 0xCD; img[35] = 0xAB;
  CHECK(validateAppImageHead(img, sizeof img, 9) == nullptr);
  CHECK(std::string(validateAppImageHead(img, sizeof img, 5)) == "OTA_IMAGE_CHIP_MISMATCH");
  img[32] = 0x50;
  CHECK(std::string(validateAppImageHead(img, sizeof img, 9)) == "OTA_IMAGE_NOT_APP");
  img[0] = 0x00;
  CHECK(std::string(validateAppImageHead(img, sizeof img, 9)) == "OTA_IMAGE_INVALID");
  CHECK(std::string(validateAppImageHead(img, 10, 9)) == "OTA_IMAGE_INVALID");
}

static SafePointInputs base() { SafePointInputs s{}; s.linkReady = true; s.uiReady = true; s.force = true; return s; }

static void safePoint() {
  auto s = base();
  CHECK(evaluateSafePoint(s) == SafePoint::SAFE);
  s.longTermSession = true;                       // open work session saved on server
  CHECK(evaluateSafePoint(s) == SafePoint::SAFE);  // forced: does not wait forever
  s.force = false;
  CHECK(evaluateSafePoint(s) == SafePoint::WAIT_SESSION);
  s = base(); s.pendingTransaction = true;         // START/FINISH not ACKed: never interrupt
  CHECK(evaluateSafePoint(s) == SafePoint::WAIT_TRANSACTION);
  s.waitingMs = BLOCKED_TRANSACTION_AFTER_MS;
  CHECK(evaluateSafePoint(s) == SafePoint::BLOCKED_TRANSACTION);
  s = base(); s.offlineEvents = 2;
  CHECK(evaluateSafePoint(s) == SafePoint::WAIT_TRANSACTION);
  s = base(); s.actionQueue = 1;
  CHECK(evaluateSafePoint(s) == SafePoint::WAIT_TRANSACTION);
  s = base(); s.uiReady = false;                   // QR hold / scan flow on screen
  CHECK(evaluateSafePoint(s) == SafePoint::WAIT_BUSY);
  s = base(); s.maintenanceOrCalibration = true;
  CHECK(evaluateSafePoint(s) == SafePoint::WAIT_BUSY);
  CHECK(std::string(safePointReason(SafePoint::BLOCKED_TRANSACTION)) == "BLOCKED_TRANSACTION");
}

int main() {
  versions(); origins(); agentBase(); image(); safePoint();
  CHECK(!eventAcknowledged(false, true, true, true));
  CHECK(!eventAcknowledged(true, false, true, true));
  CHECK(!eventAcknowledged(true, true, true, false));
  CHECK(eventAcknowledged(true, true, true, true));
  CHECK(eventAcknowledged(true, true, false, false)); // legacy Agent: no applied field

  std::printf("%d/%d checks passed\n", total - fails, total);
  return fails ? 1 : 0;
}
