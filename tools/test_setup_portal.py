"""Setup cancellation contracts; executable hardware-edge harness follows below."""
from pathlib import Path
from test_enrollment_display import function

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'esp/mesflow_app.cpp').read_text()


def test_portal_has_dedicated_input_before_early_return():
    loop = function(SOURCE, 'void loop() {')
    block = loop[loop.index('if (setupPortalActive)'):]
    block = block[:block.index('return;')]
    assert 'serviceSetupPortalInput()' in block
    for production in ('serviceKeypad()', 'handleTouch()', 'readScannerCommands()', 'readSerialCommands()'):
        assert production not in block


def test_cancel_is_non_destructive():
    cancel = function(SOURCE, 'static void cancelSetupPortal() {')
    for forbidden in ('prefs.', 'saveDeviceConfig', 'saveCurrentCredential', 'clearPendingTransaction',
                      'clearRuntimeSelection', 'LittleFS.', 'ESP.restart', 'resetForNextWorker'):
        assert forbidden not in cancel
    for required in ('captiveDns.stop()', 'setupWeb.stop()', 'WiFi.softAPdisconnect(true)',
                     'WiFi.begin(WIFI_SSID, WIFI_PASSWORD)', 'UiState::OFFLINE'):
        assert required in cancel

PREFIX = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <map>
#include <functional>
#include "mesflow_sleep_ota_gate.h"
struct String : std::string {
  using std::string::string;
  String(const std::string& v):std::string(v){}
  String substring(size_t p) const { return substr(p); }
};
unsigned now = 1000;
uint32_t millis() { return now; }
void delay(unsigned n) { now += n; }
unsigned esp_random() { static unsigned n=0; return ++n; }
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
constexpr int WIFI_AP_STA=3, WIFI_STA=1, HTTP_GET=0, HTTP_POST=1, HTTP_ANY=2, DNS_PORT=53;
constexpr int C_INFO=1, C_PANEL=2, C_TEXT=3, FONT_SECTION=16;
constexpr const char* SETUP_AP_PASSWORD="fixture-ap-only";
enum class UiState {READY, OFFLINE, INPUT_GOOD};
UiState uiState=UiState::READY;
struct Runtime { bool hasWorker=false, hasOperation=false, online=true; int activeSessionId=0; } rt;
bool pending=false, deferredFinishReset=false, pendingWebCommandReady=false;
bool keypadTestActive=false, keypadCalibrationRequested=false, keypadCalibrationInProgress=false;
bool otaInstallInProgress=false, otaCheckTaskRunning=false, otaLinkReady=true;
unsigned otaAvailableSince=0, openOpCount=0, scannerFrameLength=0;
bool wifiSetupHoldActive=true, wifiSetupHoldTriggered=true;
unsigned wifiSetupHoldStartedAt=1;
bool recoveryMenuActive=false, recoveryInfoActive=false, maintenanceMode=false;
bool maintenanceEnteredDuringBoot=false, maintenanceWebActive=false, mdnsReady=true;
bool offlineMode=false, cancelTouchActive=false;
int recoveryCountdownShown=0;
unsigned lastUserActionAt=0;
bool keypadAvailable=true, keypadMappingReady=true;
unsigned keypadLastPollAt=0, keypadCandidateSince=0;
int keypadCandidatePair=-1, keypadEmittedPair=-1;
int rawPair=-1;
int scanKeypadPair() { return rawPair; }
char keypadPairToKey(uint8_t p) { return p==1?'*':p==2?'#':'3'; }
char WIFI_SSID[33]="fixture-wrong-wifi", WIFI_PASSWORD[65]="fixture-private";
// Immutable business data: no adapter capable of writing NVS/journal is linked.
const std::string journal="pending offline event + session intent";
const std::string config="server/token/UUID/display-number";
bool hasPendingTransaction() { return pending; }
void redrawProductionUi() {}
void setUi(UiState next) { uiState=next; }
void drawFooter(const char*, const char*) {}
void drawRecoveryMenu() {}
void drawMaintenanceScreen() {}
struct SerialFake {
  unsigned bytes=0;
  int available() { return bytes; }
  int read() { --bytes; return 1; }
  void println(const char* = "") {}
  void print(const char*) {}
  template<class... T> void printf(const char*, T...) {}
} Serial, ScannerSerial;
struct WifiFake {
  int begins=0, modeValue=1; bool ap=false, persistentValue=false, reconnect=true;
  std::string ssid, password;
  void persistent(bool v) { persistentValue=v; }
  void setAutoReconnect(bool v) { reconnect=v; }
  void disconnect(bool, bool erase) { assert(!erase); }
  void mode(int v) { modeValue=v; }
  void softAP(const char*, const char*) { ap=true; }
  int softAPIP() const { return 4; }
  void softAPdisconnect(bool) { ap=false; }
  void setSleep(bool) {}
  void begin(const char* s, const char* p) { assert(!persistentValue); ++begins; ssid=s; password=p; }
} WiFi;
struct WebServer {
  bool active=false; int code=0, localIp=4, registrations=0;
  std::string nonce;
  std::map<std::string,std::function<void()>> routes;
  WebServer(int) {}
  void stop() { active=false; }
  void begin() { active=true; }
  void collectHeaders(const char**, int) {}
  template<class F> void on(const char* path, int method, F f) {
    ++registrations; routes[std::string(path)+std::to_string(method)]=f;
  }
  template<class F> void onNotFound(F) { ++registrations; }
  void sendHeader(const char*, const char*, bool=false) {}
  void send(int c, const char*, const String&) { code=c; }
  WebServer& client() { return *this; }
  int localIP() { return localIp; }
  bool hasHeader(const char*) { return !nonce.empty(); }
  String header(const char*) { return nonce; }
};
constexpr int PROVISION_HTTP_PORT=8080;
struct DNSServer { bool active=false; void start(int,const char*,int) { active=true; } void stop() { active=false; } };
struct WiFiUDP { bool active=true; void stop(){ active=false; } };
struct { void end(){} } MDNS;
struct {
  void fillRoundRect(int,int,int,int,int,int){}
  void drawRoundRect(int,int,int,int,int,int){}
} tft;
void drawSimple(const char*,const char*,const char*,const char*,int){}
std::string label;
void drawCenteredTextFit(const char* v,int,int,int,int,int,int,int,int,bool) { label=v; }
String chipSuffix(){ return "11223344"; }
String setupPage(){ return ""; }
void handleWifiScan(){}
void handlePhoneConfigSave(){}
void sendSetupRedirect(){}
mfks::OtaSleepGate otaSleepGate;
bool touch=false; uint16_t tx=0, ty=0;
bool readTouchPoint(uint16_t& x,uint16_t& y) { x=tx; y=ty; return touch; }
'''

KEYPAD_PREFIX = r'''
constexpr uint32_t WIFI_SETUP_HOLD_MS=10000, RECOVERY_MENU_HOLD_MS=5000, RECOVERY_COUNTDOWN_FROM_MS=2000;
int productionKeys=0;
void openRecoveryMenu(){recoveryMenuActive=true;}
void drawRecoveryCountdown(int){}
void handleKeypadKey(char){++productionKeys;}
void printKeypadPair(uint8_t){}
void markKeypadNeedsCalibration(const char*){}
'''

MAIN = r'''
void tick(int pair, unsigned dt=50, bool down=false, uint16_t x=20, uint16_t y=280) {
  rawPair=pair; now+=dt; touch=down; tx=x; ty=y; serviceSetupPortalInput();
}
void release() { tick(-1); tick(-1); }
void expectExit() {
  assert(!setupPortalActive && !setupWeb.active && !captiveDns.active && !WiFi.ap);
  assert(!otaSleepGate.setup && !maintenanceMode && !maintenanceWeb.active && !provisionWeb.active);
  assert(uiState==UiState::OFFLINE && offlineMode && !rt.online);
  assert(setupKeyReleaseRequired && setupTouchReleaseRequired);
  assert(!wifiSetupHoldActive && !wifiSetupHoldTriggered && !setupCancelNonce[0]);
  assert(config=="server/token/UUID/display-number" && journal=="pending offline event + session intent");
  assert(std::string(WIFI_PASSWORD)=="fixture-private");
}
int main() {
  // Refuse transactions/workflows/OTA before changing network or ownership.
  pending=true; startSetupPortal("test"); assert(!setupPortalActive && mdnsReady); pending=false;
  rt.hasWorker=true; startSetupPortal("test"); assert(!setupPortalActive); rt.hasWorker=false;
  rt.hasOperation=true; assert(!setupPortalEntryAllowed()); rt.hasOperation=false;
  rt.activeSessionId=7; assert(!setupPortalEntryAllowed()); rt.activeSessionId=0;
  openOpCount=1; assert(!setupPortalEntryAllowed()); openOpCount=0;
  uiState=UiState::INPUT_GOOD; assert(!setupPortalEntryAllowed()); uiState=UiState::READY;
  otaCheckTaskRunning=true; assert(!setupPortalEntryAllowed()); otaCheckTaskRunning=false;
  otaAvailableSince=1; assert(!setupPortalEntryAllowed()); otaAvailableSince=0;
  assert(otaSleepGate.beginOta()); startSetupPortal("test"); assert(!setupPortalActive); otaSleepGate.endOta();
  // Physical long-* entry: held key, including release, must not cancel.
  startSetupPortal("test"); assert(setupPortalActive && otaSleepGate.setup);
  assert(!otaSleepGate.beginOta() && !otaSleepGate.beginSleep());
  assert(setupWeb.active && captiveDns.active && WiFi.ap && !provisionWeb.active);
  assert(label=="#: Quay lại");
  int registrations=setupWeb.registrations;
  tick(1); tick(1,11000); assert(setupPortalActive);
  release(); assert(setupPortalActive);
  tick(1); tick(1); release(); assert(setupPortalActive); // short * deliberately inert
  tick(2,12); tick(2,20); assert(setupPortalActive); // debounce
  ScannerSerial.bytes=200;
  tick(2,40); expectExit(); assert(ScannerSerial.bytes==0);
  assert(WiFi.begins==1 && WiFi.ssid=="fixture-wrong-wifi" && WiFi.password=="fixture-private");
  assert(otaSleepGate.beginOta()); otaSleepGate.endOta();
  // No saved Wi-Fi: escape immediately, no connect attempt, repeat entry works.
  WIFI_SSID[0]=0;
  startSetupPortal("test");
  tick(2); tick(2); assert(setupPortalActive); // entry key must release
  release();
  tick(-1,50,true,11,280); tick(-1,80,true,12,280); assert(setupPortalActive); // drag from outside
  release(); tick(-1,50,true,228,280); tick(-1,80,true,228,280); assert(setupPortalActive); // right exclusive
  release(); tick(-1,50,true,20,312); tick(-1,80,true,20,312); assert(setupPortalActive); // bottom exclusive
  release(); tick(-1,50,true,12,264); tick(-1,30,true,12,264); assert(setupPortalActive);
  tick(-1,31,true,12,264); expectExit(); assert(WiFi.begins==1);
  // Held touch at entry cannot activate Back, nor can an unauthenticated phone.
  startSetupPortal("test"); tick(-1,80,true); tick(-1,80,true); assert(setupPortalActive);
  setupWeb.nonce="wrong"; handlePhoneConfigCancel(); assert(setupWeb.code==403 && !setupCancelRequested);
  setupWeb.nonce=setupCancelNonce; setupWeb.localIp=99; handlePhoneConfigCancel(); assert(setupWeb.code==403);
  setupWeb.localIp=4; handlePhoneConfigCancel(); assert(setupWeb.code==200 && setupPortalActive && setupCancelRequested);
  std::string previousNonce=setupCancelNonce;
  tick(-1); expectExit(); // deferred teardown
  for(int i=0;i<25;++i) {
    startSetupPortal("test"); assert(setupWeb.registrations==registrations);
    setupWeb.nonce=previousNonce; handlePhoneConfigCancel(); assert(setupWeb.code==403);
    previousNonce=setupCancelNonce;
    release(); tick(2); tick(2); expectExit();
  }
  assert(WiFi.begins==1);
  // Exercise the real production keypad tail after cancellation: a held '*' or
  // '#' cannot re-enter setup or flow through until a stable full release.
  keypadCandidatePair=1; now+=11000; productionKeypadTail();
  assert(!wifiSetupHoldActive && !setupPortalActive && productionKeys==0);
  keypadCandidatePair=2; productionKeypadTail(); assert(productionKeys==0);
  keypadCandidatePair=-1; productionKeypadTail(); assert(!setupKeyReleaseRequired);
  keypadCandidatePair=1; productionKeypadTail(); assert(wifiSetupHoldActive);
  keypadCandidatePair=-1; productionKeypadTail(); assert(productionKeys==1 && !setupPortalActive);
  puts("setup portal: entry guards, key/touch debounce, release, no-config, scoped phone, 25 repeat cycles PASS");
}
'''


def test_real_portal_lifecycle(tmp_path):
    import subprocess
    source = SOURCE
    declarations = source[source.index('WebServer setupWeb(80);'):source.index('// Remote diagnostic ring buffer.')]
    signatures = ['static bool setupPortalEntryAllowed() {', 'static void cancelSetupPortal() {',
                  'bool pointIn(', 'static void serviceSetupPortalInput() {',
                  'static void handlePhoneConfigCancel() {', 'void startSetupPortal(const char* reason) {']
    cpp = tmp_path / 'portal.cpp'
    keypad = function(source, 'void serviceKeypad() {')
    keypad_tail = 'void productionKeypadTail() {\n' + keypad[keypad.index('  const char stableKey'):]
    cpp.write_text(PREFIX + declarations + '\n'.join(function(source, sig) for sig in signatures) + KEYPAD_PREFIX + keypad_tail + MAIN)
    binary = tmp_path / 'portal'
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'esp'), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


def test_saved_password_is_not_rendered():
    page = function(SOURCE, 'static String setupPage() {')
    assert 'WIFI_PASSWORD' not in page
    assert "'/api/config/cancel'" in page and 'X-Setup-Cancel' in page



SAVE_PREFIX = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include <map>
#include <string>
struct String : std::string {
 using std::string::string;
 String(const std::string& s):std::string(s){}
 void trim() { auto a=find_first_not_of(" "); auto b=find_last_not_of(" "); *this=a==npos?"":substr(a,b-a+1); }
};
struct Value {
 String text; bool flag=false;
 const char* operator|(const char* fallback) const { return text.empty()?fallback:text.c_str(); }
 bool operator|(bool) const { return flag; }
 Value& operator=(const char* v) { text=v; return *this; }
 Value& operator=(const String& v) { text=v; return *this; }
 Value& operator=(bool v) { flag=v; return *this; }
};
struct DynamicJsonDocument {
 std::map<std::string, Value> data;
 DynamicJsonDocument(int){}
 Value& operator[](const char* key) { return data[key]; }
};
std::string inputSsid="fixture-current", inputPass="";
bool preserve=false, parseError=false;
using DeserializationError=bool;
bool deserializeJson(DynamicJsonDocument& d, const String&) {
 d["ssid"]=String(inputSsid); d["password"]=String(inputPass); d["preserve_password"]=preserve; return parseError;
}
void serializeJson(DynamicJsonDocument&, String&) {}
char WIFI_SSID[33]="fixture-current", WIFI_PASSWORD[65]="fixture-stored";
void safeCopy(char* dst, size_t cap, const char* src) { std::snprintf(dst,cap,"%s",src); }
struct {
 std::map<std::string,std::string> nvs={{"token","fixture-token"},{"server","fixture-server"},{"uuid","fixture-uuid"},{"display_num","2"},{"journal","event-1"}};
 int writes=0; bool writeOk=true;
 void begin(const char* ns,bool ro) { assert(std::string(ns)=="mesflow_cfg" && !ro); }
 size_t putString(const char* k,const char* v) { ++writes; if(!writeOk)return 0; nvs[k]=v; return strlen(v)+1; }
 void end(){}
} prefs;
struct {int code=0; String arg(const char*){return "";} void send(int c,const char*,const String&){code=c;} } setupWeb;
struct {int restarts=0;void restart(){++restarts;} } ESP;
void delay(unsigned n){ assert(n==1200); }
'''

SAVE_MAIN = r'''
int main() {
 const auto original=prefs.nvs;
 // Ordinary untouched form preserves the private current password.
 preserve=true; handlePhoneConfigSave();
 assert(setupWeb.code==200 && ESP.restarts==1 && prefs.nvs["wifi_pass"]=="fixture-stored");
 // Changing the password and changing SSID keep the existing save semantics.
 inputPass="fixture-new"; handlePhoneConfigSave(); assert(prefs.nvs["wifi_pass"]=="fixture-new");
 inputSsid="fixture-other"; inputPass=""; handlePhoneConfigSave(); assert(prefs.nvs["wifi_pass"].empty());
 // Explicit open-network choice / existing API request may clear a same-SSID password.
 inputPass="fixture-secured"; handlePhoneConfigSave();
 preserve=false; inputPass=""; handlePhoneConfigSave(); assert(prefs.nvs["wifi_pass"].empty());
 for(const auto& item:original) assert(prefs.nvs[item.first]==item.second);
 // Invalid requests still do not write or reboot.
 int writes=prefs.writes, restarts=ESP.restarts;
 inputSsid=""; handlePhoneConfigSave(); assert(setupWeb.code==400);
 inputSsid=std::string(33,'s'); handlePhoneConfigSave(); assert(setupWeb.code==400);
 inputSsid="fixture"; inputPass=std::string(65,'p'); handlePhoneConfigSave(); assert(setupWeb.code==400);
 parseError=true; handlePhoneConfigSave(); assert(setupWeb.code==400);
 assert(prefs.writes==writes && ESP.restarts==restarts);
 parseError=false; inputPass="fixture-new"; prefs.writeOk=false;
 handlePhoneConfigSave(); assert(setupWeb.code==500 && ESP.restarts==restarts);
 puts("save handler: private password retention, new/open Wi-Fi, validation, write failure, identity/journal unchanged PASS");
}
'''


def test_real_save_handler(tmp_path):
    import subprocess
    cpp = tmp_path / 'save.cpp'
    cpp.write_text(SAVE_PREFIX + function(SOURCE, 'static void handlePhoneConfigSave() {') + SAVE_MAIN)
    binary = tmp_path / 'save'
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


def test_back_label_uses_real_glyphs_inside_touch_bounds():
    from test_identity_header import font_data, pixels
    bitmap, glyphs, (ascent, line_height) = font_data(16)
    label = '#: Quay lại'
    width = sum(glyphs[ord(cp)][3] for cp in label)
    x, baseline = 12 + (216 - width) // 2, 264 + (48 - line_height) // 2 + ascent
    ink = []
    for cp in label:
        glyph = glyphs[ord(cp)]
        ink.extend(pixels(bitmap, glyph, x, baseline))
        x += glyph[3]
    assert all(12 <= x < 228 and 264 <= y < 312 for x, y in ink)
    assert 230 + 26 < 264  # instruction row does not intersect Back


def test_lan_routes_register_once_across_portal_cycles(tmp_path):
    import subprocess
    body = function(SOURCE, 'void startLanProvisioning() {')
    # Exercise the real registration block; network/mDNS hardware edges are omitted.
    body = body[:body.index('  provisionWeb.begin();')] + '}'
    prefix = r"""
#include <cassert>
#include <functional>
using String = const char*;
constexpr int WL_CONNECTED=1, HTTP_GET=0, HTTP_POST=1;
bool lanProvisionActive=false;
struct { int status(){return WL_CONNECTED;} } WiFi;
struct {
  int registrations=0;
  void collectHeaders(const char**, int){}
  template<class F> void on(const char*, int, F){++registrations;}
  void send(int,const char*,const char*){}
} provisionWeb;
struct {void restart(){}} ESP;
void delay(int){}
void refreshProvisionToken(){}
String webConsolePage(){return "";}
template<class T> void sendDeviceInfo(T&){}
bool provisionAuthorized(){return true;}
"""
    handlers = ('handleDeviceHealthApi handleRemoteConsoleApi handleTestDataApi '
                'handleCatalogRefreshApi handleCatalogDebugApi handleCatalogRawApi '
                'handleLanProvision').split()
    prefix += ''.join('void '+name+'(){}\n' for name in handlers)
    main = 'int main(){startLanProvisioning();int n=provisionWeb.registrations;assert(n>0);for(int i=0;i<25;++i)startLanProvisioning();assert(provisionWeb.registrations==n);}'
    cpp=tmp_path/'lan.cpp'
    cpp.write_text(prefix+body+main)
    binary=tmp_path/'lan'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
