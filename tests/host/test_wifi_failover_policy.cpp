#include "mesflow_wifi_failover_policy.h"

#include <assert.h>
#include <stdio.h>

int main() {
  const bool configured[3] = {true, true, true};
  const int32_t goodPriority[3] = {-65, -48, -35};
  assert(mfwifi::chooseKnownNetwork(configured, goodPriority) == 0);

  const int32_t strongest[3] = {-92, -75, -71};
  assert(mfwifi::chooseKnownNetwork(configured, strongest) == 2);

  const int32_t noneVisible[3] = {-127, -127, -127};
  assert(mfwifi::chooseKnownNetwork(configured, noneVisible) == -1);

  const bool sparse[3] = {false, true, false};
  assert(mfwifi::nextConfiguredNetwork(sparse, 0) == 1);
  assert(mfwifi::nextConfiguredNetwork(sparse, 1) == 1);
  assert(mfwifi::nextConfiguredNetwork(sparse, 2) == 1);

  const bool empty[3] = {false, false, false};
  assert(mfwifi::chooseKnownNetwork(empty, strongest) == -1);
  assert(mfwifi::nextConfiguredNetwork(empty, 0) == -1);
  puts("wifi failover policy: PASS");
}
