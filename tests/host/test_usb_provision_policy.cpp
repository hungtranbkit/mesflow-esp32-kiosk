#include <cstdio>
#include "mesflow_usb_provision_policy.h"
using namespace mfusb;
static int failures=0,total=0;
#define CHECK(x) do { ++total; if(!(x)){++failures; std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);} } while(0)
int main(){
  CHECK(isHex64("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
  CHECK(!isHex64("abc")); CHECK(!isHex64("zz23456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
  char id[32], mac[18];
  CHECK(canonicalMac("84:c7:bb:79:39:6c",mac)); CHECK(strcmp(mac,"84:C7:BB:79:39:6C")==0);
  CHECK(deriveDeviceId(mac,id)); CHECK(strcmp(id,"ESP32-KIOSK-396C")==0);
  CHECK(!deriveDeviceId("not-a-mac",id));
  CHECK(mayProvision(true,true,false,true,false,false));
  CHECK(!mayProvision(false,true,false,true,false,false)); CHECK(!mayProvision(true,false,false,true,false,false));
  CHECK(!mayProvision(true,true,true,true,false,false)); CHECK(!mayProvision(true,true,false,false,false,false));
  CHECK(!mayProvision(true,true,false,true,true,false)); CHECK(!mayProvision(true,true,false,true,false,true));
  CHECK(factoryOrSameId("ESP32-KIOSK-001",id)); CHECK(factoryOrSameId(id,id)); CHECK(!factoryOrSameId("ESP32-KIOSK-0001",id));
  CHECK(mayAutoConfigure(true,false,false,true,false,false,false,true,true));
  CHECK(!mayAutoConfigure(false,false,false,true,false,false,false,true,true));
  CHECK(!mayAutoConfigure(true,true,false,true,false,false,false,true,true));
  CHECK(!mayAutoConfigure(true,false,true,true,false,false,false,true,true));
  CHECK(!mayAutoConfigure(true,false,false,true,false,false,true,true,true));
  CHECK(configValueUnchangedOrFactoryDefault("","Kimex-WiFi",""));
  CHECK(configValueUnchangedOrFactoryDefault("Kimex-WiFi","Kimex-WiFi",""));
  CHECK(!configValueUnchangedOrFactoryDefault("Existing-WiFi","Kimex-WiFi",""));
  std::printf("%d/%d checks passed\n",total-failures,total); return failures?1:0;
}
