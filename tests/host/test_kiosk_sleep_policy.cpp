#include <cassert>
#include <cstdio>
#include "mesflow_kiosk_sleep_policy.h"
int main() {
 using namespace mfks;
 const Config c{true,1320,360};
 assert(waitSeconds(c,false, 23*3600)==0);
 assert(waitSeconds(c,true,21*3600+59*60)==0);
 assert(waitSeconds(c,true,22*3600)==8*3600);
 assert(waitSeconds(c,true,23*3600+59*60)==6*3600+60);
 assert(waitSeconds(c,true,0)==6*3600);
 assert(waitSeconds(c,true,5*3600+59*60)==60);
 assert(waitSeconds(c,true,6*3600)==0);
 assert(waitSeconds(c,true,6*3600+1)==0);
 assert(waitSeconds(Config{false,1320,360},true,23*3600)==0);
 assert(waitSeconds(Config{true,360,360},true,23*3600)==0);
 assert(waitSeconds(Config{true,1440,360},true,23*3600)==0);
 assert(waitSeconds(Config{true,480,1020},true,600*60)==420*60);
 assert(waitSeconds(c,true,23*3600)==7*3600); // reboot
 assert(waitSeconds(c,true,12*3600)==0); // jump to daytime
 assert(waitSeconds(c,true,3*3600)==3*3600); // jump into window
 assert(!maySleep(true,true,true,false));
 assert(!maySleep(true,true,false,true));
 assert(!maySleep(false,true,false,false));
 assert(maySleep(true,true,false,false));
 puts("deep sleep policy: 19 checks passed");
}
