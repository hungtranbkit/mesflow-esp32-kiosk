#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <time.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <WebServer.h>
#include <stdarg.h>
#include <DNSServer.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <Update.h>
#include <mbedtls/sha256.h>
#include <esp_task_wdt.h>
#include "mesflow_vietnamese_font.h"

#ifndef MESFLOW_UI_SCREENSHOT
#define MESFLOW_UI_SCREENSHOT 1
#endif
#if defined(__has_include)
#if __has_include("mesflow_ota_ca.h")
#include "mesflow_ota_ca.h"
#endif
#endif
#ifndef MESFLOW_ROOT_CA_PEM
#define MESFLOW_ROOT_CA_PEM ""
#endif

// ArduinoJson allocator backed by ESP32-S3 PSRAM.
// Catalog responses can be tens or hundreds of KB; keeping this document in
// internal heap caused deserializeJson() to fail with NoMemory.
struct PsramJsonAllocator {
  void* allocate(size_t size) {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    return p;
  }
  void deallocate(void* pointer) { heap_caps_free(pointer); }
  void* reallocate(void* pointer, size_t newSize) {
    void* p = heap_caps_realloc(pointer, newSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_realloc(pointer, newSize, MALLOC_CAP_8BIT);
    return p;
  }
};
using PsramJsonDocument = BasicJsonDocument<PsramJsonAllocator>;

// ============================================================
// MESFlow ESP32-S3 production kiosk with durable WiFi transaction journal
// Board: ES3C28P / ESP32-S3 N16R8
// LCD: ILI9341 240x320 portrait (forced)
// Input tam thoi: Serial Monitor; logic phu hop scanner/numpad
// ============================================================

// -------------------- RUNTIME CONFIG -------------------------
// Defaults are used only on first boot. They are overwritten by NVS after
// phone setup or LAN provisioning from MESFlow Admin Center.
char WIFI_SSID[33]       = "panda";
char WIFI_PASSWORD[65]   = "12345678a";
char SERVER_BASE[192]    = "";  // Bat buoc cau hinh qua Setup Portal/NVS; khong co fallback hardcode
// Deploy Agent is the OTA control plane. NVS/maintenance can override this
// per environment, but a fresh kiosk is immediately OTA-capable by default.
char OTA_AGENT_BASE[192] = "https://deploy.mesflow.net/agent";
char DEVICE_ID[48]       = "ESP32-KIOSK-001";
char DEVICE_NAME[64]     = "ESP32 Kiosk Demo 01";
char STATION_CODE[40]    = "LASER-01";
char DEVICE_UUID[40]      = "";   // identity vinh vien, namespace mf_identity
char DEVICE_SECRET[65]    = "";   // 256-bit secret; khong hien tren UI/log
#define FW_VERSION "5.5.7"
#define FW_BUILD "20260813.0015"
#define HW_MODEL "ES3C28P"
const char* APP_VERSION  = "ESP32-KIOSK-5.5.7-KIMEX-OTA-DEFAULT";

constexpr uint16_t DISCOVERY_PORT = 17891;
constexpr uint16_t PROVISION_HTTP_PORT = 17892;
constexpr uint8_t DNS_PORT = 53;
constexpr uint32_t DISCOVERY_INTERVAL_MS = 5000;
constexpr char SETUP_AP_PASSWORD[] = "mesflow123";

// QR giả lập nhưng phải là QR có thật trong database đang chạy.
// Có thể dán QR qua Serial Monitor, không cần sửa và compile lại.
char demoWorkerQr[192] = "WF|EMP|NV001";
char demoOperationQr[256] = "";

// Sản lượng giả lập, cũng có thể đổi qua Serial Monitor.
int demoGoodQty = 0;
int demoReworkQty = 0;
int demoDefectQty = 0;

// true: nếu chưa có token hoặc token hết hiệu lực thì tự bind.
constexpr bool AUTO_BIND = true;
// Use plain HTTP for mesflow.net to reduce TLS heap/stack pressure on boards without PSRAM.
constexpr bool PREFER_PLAIN_HTTP_FOR_MESFLOW = true;

// Tu dong mo phong: quet worker -> quet operation -> start -> nhap SL -> finish.
constexpr bool AUTO_DEMO_ON_BOOT = false; // Da tat hoan toan
constexpr uint32_t AUTO_STEP_DELAY_MS = 1300;
constexpr uint32_t AUTO_WORK_TIME_MS = 6000;
constexpr uint32_t SIM_KEY_DELAY_MS = 500;
constexpr bool AUTO_REPEAT_DEMO = false;

// Sau khi finish thanh cong, hien thong bao ngan roi tra ve man hinh
// cho quet the nhan vien moi, giong kiosk MESFlow hien co.
// Thoi gian de cong nhan kip doc va kiem tra man hinh.
// Luong kiosk toi gian: lookup nhanh, START giu 10 giay de cong nhan kip doc,
// cac loi thong thuong chi giu ngan roi tra ve QUET THE.
constexpr uint32_t OP_REVIEW_HOLD_MS      = 120;
constexpr uint32_t START_SUCCESS_HOLD_MS  = 10000;
constexpr uint32_t BUSINESS_ERROR_HOLD_MS = 2000;
constexpr uint32_t STATE_WATCHDOG_CHECK_MS = 500;
constexpr uint32_t UI_CANCEL_HOLD_MS = 1800;
constexpr uint32_t WIFI_SETUP_HOLD_MS = 10000;
constexpr uint32_t WIFI_SETUP_BOOT_VERIFY_MS = 1500;
constexpr uint32_t FINISH_SUCCESS_HOLD_MS = 0; // Finish xong ve QUET THE ngay

// -------------------- LCD PINS -------------------------------
#define TFT_MISO 13
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC   46
#define TFT_RST  -1
#define TFT_BL   45

// UART scanner connector P2 on ES3C28P / ES3N28P.
// Wiring verified by standalone scanner test:
//   GM865 TX -> ESP GPIO44 (RX)
//   GM865 RX -> not connected
#define SCANNER_RX_PIN 44
// 2026-08-30: this unit's module-side baud (its own EEPROM setting, not the
// GM65's factory default) measured at 115200 via edge-capture + linear
// regression against a USB-Virtual-Serial-Port ground-truth readback of
// "WF|EMP|NV002", then reconfirmed clean over the real UART1 peripheral
// (13/13 frames exact). 9600 (the datasheet's factory default) produced
// zero bytes on real hardware.
constexpr uint32_t SCANNER_BAUD = 115200;
constexpr uint32_t SCANNER_FRAME_TIMEOUT_MS = 50;
constexpr size_t SCANNER_FRAME_MAX = 256;
HardwareSerial ScannerSerial(1);
static uint8_t scannerFrame[SCANNER_FRAME_MAX] = {0};
static size_t scannerFrameLength = 0;
static uint32_t scannerLastByteAt = 0;
static uint32_t scannerByteCount = 0;
static uint32_t scannerFrameCount = 0;

// Capacitive touch (FT6336G capacitive touch, I2C address 0x38)
#define TOUCH_SDA 16
#define TOUCH_SCL 15
#define TOUCH_RST 18
#define TOUCH_INT 17
constexpr uint8_t TOUCH_ADDR = 0x38;
TwoWire touchWire = TwoWire(0);

// 3x4 matrix keypad through PCF8574T. The keypad shares the board's existing
// I2C bus with the FT6336G touch controller. A custom pair scanner allows the
// seven keypad wires to be connected to P0..P7 in any order. One guided
// calibration maps the 12 physical keys and stores the result in NVS.
constexpr uint8_t KEYPAD_ADDRESS_FIRST = 0x20;
constexpr uint8_t KEYPAD_ADDRESS_LAST  = 0x27;
constexpr uint8_t KEYPAD_KEY_COUNT = 12;
constexpr uint32_t KEYPAD_MAPPING_MAGIC = 0x4B503334UL;
constexpr char KEYPAD_NVS_NAMESPACE[] = "mf_keypad";
const char keypadCalibrationKeys[KEYPAD_KEY_COUNT + 1] = "123456789*0#";
uint8_t keypadPairs[KEYPAD_KEY_COUNT] = {0};
bool keypadAvailable = false;
bool keypadMappingReady = false;
uint8_t keypadAddress = 0;
char keypadNumberBuffer[7] = "";  // 0..999999
uint8_t keypadNumberLength = 0;
uint8_t keypadBufferState = 0xFF;
uint32_t keypadLastPollAt = 0;
int keypadCandidatePair = -1;
int keypadEmittedPair = -1;
uint32_t keypadCandidateSince = 0;
bool keypadCalibrationRequested = false;
bool keypadCalibrationInProgress = false;
char keypadCalibrationRequestSource[16] = "";
bool bootWifiSetupRequested = false;
bool wifiSetupHoldActive = false;
bool wifiSetupHoldTriggered = false;
uint32_t wifiSetupHoldStartedAt = 0;
bool maintenanceMode = false;
bool maintenanceEnteredDuringBoot = false;
bool maintenanceWebActive = false;
uint32_t lastMaintenanceRefreshAt = 0;
char maintenanceLastIp[16] = "";
char mdnsHostname[64] = "";
bool mdnsReady = false;
int16_t maintenancePendingOverride = -1;

static uint16_t* uiShadowFramebuffer = nullptr;

class MesflowDisplay : public Adafruit_ILI9341 {
public:
  MesflowDisplay(SPIClass* spi, int8_t dc, int8_t cs, int8_t rst)
      : Adafruit_ILI9341(spi, dc, cs, rst) {}
  void drawPixel(int16_t x, int16_t y, uint16_t color) override { Adafruit_ILI9341::drawPixel(x,y,color); mirrorPixel(x,y,color); }
  void writePixel(int16_t x, int16_t y, uint16_t color) override { Adafruit_ILI9341::writePixel(x,y,color); mirrorPixel(x,y,color); }
  void drawFastHLine(int16_t x,int16_t y,int16_t w,uint16_t color) override { Adafruit_ILI9341::drawFastHLine(x,y,w,color); mirrorRect(x,y,w,1,color); }
  void drawFastVLine(int16_t x,int16_t y,int16_t h,uint16_t color) override { Adafruit_ILI9341::drawFastVLine(x,y,h,color); mirrorRect(x,y,1,h,color); }
  void writeFastHLine(int16_t x,int16_t y,int16_t w,uint16_t color) override { Adafruit_ILI9341::writeFastHLine(x,y,w,color); mirrorRect(x,y,w,1,color); }
  void writeFastVLine(int16_t x,int16_t y,int16_t h,uint16_t color) override { Adafruit_ILI9341::writeFastVLine(x,y,h,color); mirrorRect(x,y,1,h,color); }
  void writeFillRect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t color) override { Adafruit_ILI9341::writeFillRect(x,y,w,h,color); mirrorRect(x,y,w,h,color); }
  void fillRect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t color) override { Adafruit_ILI9341::fillRect(x,y,w,h,color); mirrorRect(x,y,w,h,color); }
  void fillScreen(uint16_t color) override { Adafruit_ILI9341::fillScreen(color); mirrorRect(0,0,240,320,color); }

private:
  static void mirrorPixel(int16_t x, int16_t y, uint16_t color) {
#if MESFLOW_UI_SCREENSHOT
    if (uiShadowFramebuffer && x >= 0 && x < 240 && y >= 0 && y < 320)
      uiShadowFramebuffer[y * 240 + x] = color;
#endif
  }
  static void mirrorRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
#if MESFLOW_UI_SCREENSHOT
    if (!uiShadowFramebuffer || w <= 0 || h <= 0) return;
    const int16_t x0 = max<int16_t>(0, x), y0 = max<int16_t>(0, y);
    const int16_t x1 = min<int16_t>(240, x + w), y1 = min<int16_t>(320, y + h);
    for (int16_t yy = y0; yy < y1; ++yy)
      for (int16_t xx = x0; xx < x1; ++xx) uiShadowFramebuffer[yy * 240 + xx] = color;
#endif
  }
};

SPIClass lcdSPI(FSPI);
MesflowDisplay tft(&lcdSPI, TFT_DC, TFT_CS, TFT_RST);
Preferences prefs;

// Web Device Manager telemetry (technical data is shown only on the web console).
static uint32_t lastInputAtMs = 0;
static uint32_t inputEventCount = 0;
static bool lastInputWasVirtual = false;
static bool lastInputWasKeypad = false;
static char lastInputPreview[72] = "";
static uint32_t lastHealthRequestAtMs = 0;

// -------------------- UI -------------------------------------
constexpr uint8_t LCD_ROTATION = 0;
constexpr int16_t SW = 240;
constexpr int16_t SH = 320;
constexpr int16_t HEADER_H = 36;
constexpr int16_t SCREEN_LEFT_MARGIN = 12;
constexpr int16_t SCREEN_RIGHT_MARGIN = 12;
constexpr int16_t HEADER_Y = 0;
constexpr int16_t TITLE_Y = 52;
constexpr int16_t CONTENT_TOP = 94;
constexpr int16_t CONTENT_BOTTOM = 260;
constexpr int16_t FOOTER_Y = 272;
constexpr int16_t FOOTER_H = 48;
constexpr int16_t UI_MARGIN = SCREEN_LEFT_MARGIN;
constexpr uint8_t FONT_HEADER = 1;
constexpr uint8_t FONT_TITLE = 3;
constexpr uint8_t FONT_SECTION = 2;
constexpr uint8_t FONT_VALUE = 3;
constexpr uint8_t FONT_OPTION = 2;
constexpr uint8_t FONT_FOOTER = 1;
constexpr uint8_t FONT_SECONDARY = FONT_SECTION;
constexpr uint8_t FONT_BODY = FONT_SECTION;
constexpr uint8_t FONT_NAME = FONT_VALUE;
constexpr uint8_t FONT_QUANTITY = 8;
constexpr uint8_t FONT_ERROR = FONT_TITLE;

// Subtle industrial palette: mostly neutral, color only for status emphasis.
// MESFlow v5 unified midnight-blue theme (RGB565)
// Every screen uses the same dark surface, white typography and blue line icons.
constexpr uint16_t C_BG      = 0x0021; // #05070B near-black
constexpr uint16_t C_PANEL   = 0x0863; // #0B1220 deep navy
constexpr uint16_t C_PANEL_2 = 0x2148; // #1E293B slate border
constexpr uint16_t C_HEADER  = 0x3C1F; // #3B82F6 primary blue
constexpr uint16_t C_TEXT    = 0xFFFF; // #FFFFFF
constexpr uint16_t C_MUTED   = 0x9CF3; // #94A3B8
constexpr uint16_t C_OK      = 0x2E6B; // #22C55E status only
constexpr uint16_t C_WARN    = 0x64BF; // #60A5FA blue attention, no orange
constexpr uint16_t C_ERR     = 0xF2AA; // #F43F5E status only
constexpr uint16_t C_INFO    = 0x64BF; // #60A5FA secondary blue

// -------------------- FORWARD DECLARATIONS -------------------
// Functions used by subsystems before their implementation later in this file.
void clearRuntimeSelection();
void startSetupPortal(const char* reason);
void startLanProvisioning();
void addAuthHeaders(HTTPClient& http);
void serviceDeviceManagement();
bool loadDeviceConfig();
bool loadOrCreateDeviceIdentity();
bool saveDeviceConfig();
void serviceStateWatchdog();
void recoverUiFromStuck(const char* reason, bool userRequested = false);
bool sendKioskEvent(const char* eventType, const char* severity, const char* message,
                    const char* recoveryAction, uint32_t stateAgeMs);
void beginSessionTrace();
void endSessionTrace();
bool emitActionEvent(const char* eventType, const char* category,
                     const char* result = nullptr, int httpStatus = 0,
                     uint32_t durationMs = 0, const char* message = nullptr,
                     const char* errorCode = nullptr, const char* inputType = nullptr);
void serviceActionEventQueue();
void readScannerCommands();
static bool requestRuntimeKeypadCalibration(const char* source);
static void serviceRuntimeKeypadCalibration();
static void enterMaintenanceMode(bool duringBoot);
static void exitMaintenanceMode();
static void drawMaintenanceScreen();
static void serviceMaintenanceMode();
#if MESFLOW_UI_SCREENSHOT
static void handleDebugScreenshot();
static void handleDebugUiState();
static void handleDebugScreens();
static void handleDebugShowScreen();
#endif


// -------------------- UNIFIED HTTP/HTTPS TRANSPORT ----------
// One transport wrapper for LAN HTTP and Internet HTTPS.
// HTTPClient creates Host, Content-Length/Transfer-Encoding and Connection.
class MesHttpSession {
public:
  bool begin(const String& url,
             uint16_t connectTimeoutMs,
             uint16_t requestTimeoutMs,
             uint16_t clientTimeoutSeconds,
             bool useHttp10 = true,
             bool followRedirects = false) {
    end();
    secure_ = url.startsWith("https://");

    plainClient_.setTimeout(clientTimeoutSeconds);
    // Public Internet HTTPS uses the ESP-IDF certificate bundle. Never fall
    // back to setInsecure(): an untrusted OTA endpoint must fail closed.
    if (secure_ && strlen(MESFLOW_ROOT_CA_PEM) == 0) return false;
    if (secure_) secureClient_.setCACert(MESFLOW_ROOT_CA_PEM);
    secureClient_.setTimeout(clientTimeoutSeconds);

    http_.setConnectTimeout(connectTimeoutMs);
    http_.setTimeout(requestTimeoutMs);
    http_.setReuse(false);
    http_.useHTTP10(useHttp10);
    if (followRedirects) {
      http_.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    }

    begun_ = secure_
        ? http_.begin(secureClient_, url)
        : http_.begin(plainClient_, url);
    return begun_;
  }

  HTTPClient& http() { return http_; }
  bool isSecure() const { return secure_; }

  void end() {
    if (begun_) http_.end();
    plainClient_.stop();
    secureClient_.stop();
    begun_ = false;
  }

  ~MesHttpSession() { end(); }

private:
  WiFiClient plainClient_;
  WiFiClientSecure secureClient_;
  HTTPClient http_;
  bool secure_ = false;
  bool begun_ = false;
};

static void addJsonHeaders(HTTPClient& http) {
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.addHeader("User-Agent", "MESFlow-ESP32/4.0.13-LIGHT-HTTP-LOOKUP");
}

// -------------------- STATE ----------------------------------
enum class UiState : uint8_t {
  BOOT,
  WIFI,
  BINDING,
  READY,
  SIM_SCAN_WORKER,
  LOOKUP_WORKER,
  WORKER_OK,
  SIM_SCAN_OPERATION,
  LOOKUP_OPERATION,
  OPERATION_OK,
  STARTING,
  START_SUCCESS,
  ACTIVE_SESSION,
  INPUT_GOOD,
  INPUT_DEFECT,
  ASK_REWORK,
  INPUT_REWORK,
  CONFIRM_QTY,
  FINISHING,
  FINISH_RETRY,
  FINISH_SUCCESS,
  ERROR_STATE,
  OFFLINE,
  SYNC_PENDING,
  TOUCH_TEST
};

UiState uiState = UiState::BOOT;
uint32_t stateEnteredAt = 0;
bool returnToReadyAfterError = false;
uint32_t lastHeartbeatAt = 0;
constexpr uint32_t HEARTBEAT_MS = 20000;
// OTA polling is deliberately short for kiosk/test operation.  A successful
// check/no-update sleeps 12s; network/Agent errors retry after 25s.
constexpr uint32_t OTA_CHECK_INTERVAL_MS = 12UL * 1000UL;
constexpr uint32_t OTA_RETRY_INTERVAL_MS = 25UL * 1000UL;
uint32_t lastOtaCheckAt = 0;
bool otaAvailableWaitingIdle = false;
bool otaCheckSucceeded = false;
volatile bool otaCheckTaskRunning = false;
bool otaLinkReady = false;
char otaFirmwareId[40] = "", otaTargetVersion[40] = "", otaTargetBuild[40] = "";
char otaDownloadUrl[384] = "", otaExpectedSha256[65] = "";
size_t otaExpectedSize = 0;
// Giai phong kiosk neu cong nhan quet the de ket thuc session roi bo di.
// Chi reset giao dien cuc bo; KHONG tu dong finish session tren server.
constexpr uint32_t QUANTITY_INPUT_IDLE_TIMEOUT_MS = 120000;
constexpr uint32_t QUANTITY_CONFIRM_IDLE_TIMEOUT_MS = 60000;
constexpr uint8_t HEARTBEAT_FAILS_TO_OFFLINE = 3;
uint8_t heartbeatFailCount = 0;
uint32_t lastStateWatchdogAt = 0;
uint32_t lastUserActionAt = 0;
uint32_t lastLoopAliveAt = 0;
// FINISH UI cleanup must run after HTTP/ArduinoJson stack frames are released.
volatile bool deferredFinishReset = false;
uint32_t stateRecoveryCount = 0;
uint32_t lastRecoveryAt = 0;
char lastRecoveryReason[64] = "";
uint32_t cancelTouchStartedAt = 0;
bool cancelTouchActive = false;

// -------------------- LIVE ACTIVITY --------------------------
// Action events are best-effort telemetry. They never block or replace the
// durable START/FINISH transaction journal.
constexpr char ACTION_QUEUE_PATH[] = "/kiosk_action_queue.jsonl";
constexpr char ACTION_QUEUE_TMP_PATH[] = "/kiosk_action_queue.tmp";
constexpr uint16_t ACTION_QUEUE_MAX = 100;
constexpr uint32_t ACTION_QUEUE_RETRY_MS = 15000;
char sessionTraceId[96] = "";
uint32_t clientEventCounter = 0;
uint32_t lastActionQueueRetryAt = 0;
uint32_t actionQueueDropped = 0;

// -------------------- TOUCH TEST -----------------------------
bool touchAvailable = false;
bool touchWasDown = false;
uint16_t touchX = 0, touchY = 0;
uint32_t touchCount = 0;
uint16_t touchButtonCount[4] = {0, 0, 0, 0};
int16_t touchSliderValue = 50;

// -------------------- CLOCK ----------------------------------
// Clock is synchronized from heartbeat server_epoch when available.
// NTP remains a fallback. The screen only redraws the clock when the minute
// changes, so it does not flicker or waste SPI bandwidth.
constexpr time_t MIN_VALID_EPOCH = 1700000000;
// 240px kiosk header: keep the clock centered and reserve the right edge for
// the Wi-Fi signal indicator. Kimex remains anchored at the left.
constexpr int16_t CLOCK_X = 88;
constexpr int16_t CLOCK_Y = 6;
constexpr int16_t CLOCK_W = 64;
constexpr int16_t CLOCK_H = 22;

// Header network indicator: blinking green means Wi-Fi + MES connection are alive.
constexpr int16_t NET_LED_X = 220;
constexpr int16_t NET_LED_Y = 19;
constexpr uint32_t NET_LED_BLINK_MS = 1200;
uint32_t lastNetLedBlinkAt = 0;
bool netLedOn = false;
bool lastNetConnected = false;
int8_t lastWifiBars = -1;
uint8_t lastDrawnServerState = 0xFF;
uint32_t lastClockCheckAt = 0;
int lastClockMinuteKey = -1;

struct RuntimeData {
  char kioskToken[128] = "";

  // Runtime state is explicit. IDs from MESFlow may be strings, so the
  // kiosk must never use numeric id == 0 to decide whether a scan exists.
  bool hasWorker = false;
  bool hasOperation = false;

  int workerId = 0;
  char workerQr[96] = "";
  char workerCode[32] = "";
  char workerName[64] = "";

  int operationId = 0;
  char operationQr[96] = "";
  char operationCode[48] = "";
  char operationName[72] = "";
  char po[40] = "";
  char part[40] = "";

  int activeSessionId = 0;
  char activeGroupId[128] = "";
  char activeStartTime[40] = "";

  char batchToken[128] = "";
  char finishToken[128] = "";

  int lastHttpStatus = 0;
  char lastError[160] = "";

  bool bound = false;
  bool online = false;
};

RuntimeData rt;


// ============================================================
// Durable transaction journal
// A START/FINISH request is written to NVS BEFORE it is sent. It is removed
// only after the server confirms HTTP 2xx. Therefore a WiFi drop, timeout or
// reboot cannot silently lose a production transaction.
// To keep behavior deterministic on a shared kiosk, only one unresolved
// transaction is allowed; the kiosk waits for synchronization before accepting
// another worker.
// ============================================================
enum class PendingType : uint8_t { NONE = 0, START = 1, FINISH = 2 };

struct PendingTransaction {
  uint32_t magic = 0x4D465458; // "MFTX"
  uint16_t version = 1;
  uint8_t type = 0;
  uint8_t reserved = 0;
  char token[128] = "";
  char workerQr[96] = "";
  char workerName[64] = "";
  char operationQr[96] = "";
  char operationName[72] = "";
  char groupId[128] = "";
  int32_t sessionId = 0;
  int32_t goodQty = 0;
  int32_t defectQty = 0;
  uint32_t createdUptime = 0;
  uint32_t checksum = 0;
};

PendingTransaction pendingTx;
uint32_t lastPendingRetryAt = 0;
constexpr uint32_t PENDING_RETRY_MS = 10000;

// Offline mode starts only after a sustained outage, avoiding mode flapping.
constexpr uint32_t OFFLINE_ENTER_AFTER_MS = 45000;
constexpr uint32_t OFFLINE_SYNC_RETRY_MS = 5000;
// A stale snapshot remains usable offline and is audited by its revision.
constexpr uint16_t MAX_OFFLINE_EVENTS = 500;
constexpr uint16_t MAX_CACHED_WORKERS = 160;
constexpr uint16_t MAX_CACHED_OPERATIONS = 320;
constexpr uint16_t MAX_OFFLINE_SESSIONS = 80;
const char* OFFLINE_SYNC_PATH = "/api/station/events/sync";

uint32_t fnv1a32(const uint8_t* data, size_t len) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < len; ++i) {
    hash ^= data[i];
    hash *= 16777619UL;
  }
  return hash;
}

uint32_t pendingChecksum(const PendingTransaction& tx) {
  PendingTransaction copy = tx;
  copy.checksum = 0;
  return fnv1a32(reinterpret_cast<const uint8_t*>(&copy), sizeof(copy));
}

bool hasPendingTransaction() {
  return pendingTx.magic == 0x4D465458 &&
         pendingTx.version == 1 &&
         pendingTx.type != static_cast<uint8_t>(PendingType::NONE) &&
         pendingTx.checksum == pendingChecksum(pendingTx);
}

bool savePendingTransaction() {
  pendingTx.magic = 0x4D465458;
  pendingTx.version = 1;
  pendingTx.checksum = pendingChecksum(pendingTx);
  prefs.begin("mesflow", false);
  size_t written = prefs.putBytes("pending_tx", &pendingTx, sizeof(pendingTx));
  prefs.end();
  bool ok = written == sizeof(pendingTx);
  Serial.printf("[JOURNAL] save type=%u token=%s bytes=%u %s\n",
                pendingTx.type, pendingTx.token,
                static_cast<unsigned>(written), ok ? "OK" : "FAIL");
  return ok;
}

void clearPendingTransaction() {
  prefs.begin("mesflow", false);
  prefs.remove("pending_tx");
  prefs.end();
  pendingTx = PendingTransaction();
  pendingTx.type = static_cast<uint8_t>(PendingType::NONE);
  pendingTx.checksum = pendingChecksum(pendingTx);
  Serial.println("[JOURNAL] cleared");
}

bool loadPendingTransaction() {
  prefs.begin("mesflow", true);
  size_t len = prefs.getBytesLength("pending_tx");
  size_t read = 0;
  if (len == sizeof(PendingTransaction)) {
    read = prefs.getBytes("pending_tx", &pendingTx, sizeof(pendingTx));
  }
  prefs.end();

  if (read == sizeof(PendingTransaction) && hasPendingTransaction()) {
    Serial.printf("[JOURNAL] recovered type=%u token=%s session=%ld\n",
                  pendingTx.type, pendingTx.token,
                  static_cast<long>(pendingTx.sessionId));
    return true;
  }

  if (len > 0) {
    Serial.println("[JOURNAL] invalid/corrupt pending transaction; keeping kiosk blocked");
    pendingTx = PendingTransaction();
    pendingTx.type = static_cast<uint8_t>(PendingType::FINISH);
    strlcpy(pendingTx.token, "CORRUPT-JOURNAL", sizeof(pendingTx.token));
    pendingTx.checksum = pendingChecksum(pendingTx);
    return true;
  }
  return false;
}


// Forward declarations used by the offline subsystem.
void safeCopy(char* dst, size_t size, const char* src);
void setError(int status, const char* message);
void setUi(UiState next);
void resetForNextWorker();
void drawSimple(const char* title, const char* line1, const char* line2, const char* footer, uint16_t accent);
void drawTopClock(bool force = false);
void serviceNetworkIndicator(bool force = false);
void drawError();
void syncClockFromServer(uint32_t serverEpoch);
void handleSerialLine(String line);
bool httpPostJson(const char* path, DynamicJsonDocument& request, DynamicJsonDocument& response, bool auth, bool quick = false, const char* baseOverride = nullptr);

// ============================================================
// Offline cache + append-only event journal (LittleFS)
// ============================================================
constexpr uint32_t OFF_MAGIC = 0x4D464F46; // MFOF
constexpr uint16_t OFF_VERSION = 1;
const char* WORKER_CACHE_FILE = "/workers.bin";
const char* OP_CACHE_FILE = "/operations.bin";
const char* SESSION_FILE = "/offline_sessions.bin";
const char* EVENT_LOG_FILE = "/offline_events.log";

struct CachedWorker {
  uint32_t magic = OFF_MAGIC;
  char qr[96] = "";
  char code[32] = "";
  char name[64] = "";
  uint32_t cachedEpoch = 0;
  uint32_t checksum = 0;
};
struct CachedOperation {
  uint32_t magic = OFF_MAGIC;
  char qr[96] = "";
  char name[72] = "";
  char po[40] = "";
  char part[40] = "";
  char station[32] = "";
  uint32_t cachedEpoch = 0;
  uint32_t checksum = 0;
};
struct OfflineSession {
  uint32_t magic = OFF_MAGIC;
  char localSessionId[56] = "";
  char workerQr[96] = "";
  char workerName[64] = "";
  char operationQr[96] = "";
  char operationName[72] = "";
  uint32_t startSequence = 0;
  uint32_t startEpoch = 0;
  char startEventId[64] = "";
  uint8_t finishPending = 0;
  char finishEventId[64] = "";
  uint32_t finishSequence = 0;
  int32_t finishGoodQty = 0;
  int32_t finishDefectQty = 0;
  uint32_t finishEpoch = 0;
  uint32_t checksum = 0;
};
enum class OfflineEventType : uint8_t { START = 1, FINISH = 2 };
enum class LogRecordType : uint8_t { EVENT = 1, ACK = 2, REJECT = 3 };
struct OfflineLogRecord {
  uint32_t magic = OFF_MAGIC;
  uint16_t version = OFF_VERSION;
  uint8_t recordType = 0;
  uint8_t eventType = 0;
  char eventId[64] = "";
  char localSessionId[56] = "";
  char workerQr[96] = "";
  char workerName[64] = "";
  char operationQr[96] = "";
  char operationName[72] = "";
  int32_t goodQty = 0;
  int32_t defectQty = 0;
  uint32_t sequence = 0;
  uint32_t eventEpoch = 0;
  uint32_t bootId = 0;
  uint32_t checksum = 0;
};

// Large offline buffers live in PSRAM so Wi-Fi/HTTP/JSON keep enough internal SRAM.
CachedWorker* workerCache = nullptr;
CachedOperation* operationCache = nullptr;
OfflineSession* offlineSessions = nullptr;
char (*offlineAckScratch)[64] = nullptr;
uint16_t workerCacheCount = 0, operationCacheCount = 0, offlineSessionCount = 0;
uint32_t wifiLostAt = 0, lastOfflineSyncAt = 0, bootId = 0;
uint8_t offlineSyncFailures = 0;
uint32_t offlineNextSyncAt = 0;
uint32_t lastOfflineSyncEpoch = 0;
uint32_t lastCatalogAutoRefreshAt = 0, lastCatalogAutoAttemptAt = 0;
bool offlineMode = false, fsReady = false, offlineBuffersReady = false;
char offlineSnapshotRevision[32] = "unknown";

// FORENSICS (2026-09-09): field report "de lau, quet ma lai la bi reset" --
// the kiosk sits idle, then a scan appears to reboot it. Three different
// mechanisms can produce that same visible symptom and they need completely
// different fixes:
//   1. task-watchdog force-reboot (setup()'s 40s net, armed 2026-08-22 round
//      5) fired because a blocking network call -- DNS in particular, which
//      is NOT bounded by our connect/request timeouts -- hung the main loop;
//   2. brownout: the scanner's illumination LED current spike (plus a WiFi
//      re-association burst right after idle) sagging a marginal 5V supply;
//   3. no reboot at all -- WiFi was simply down at that instant, the scan
//      failed with "WiFi chua ket noi", and the operator had to scan again.
// esp_reset_reason() tells 1 and 2 apart definitively, and the WiFi outage
// counters below tell 3 apart from both. All of it was previously only
// reachable inside a bind/heartbeat payload -- and kiosk_events /
// kiosk_client_events are empty (0 rows) in every MESFlow database on this
// host, so in practice nothing was recorded anywhere. Surfaced in
// buildRemoteStatusText() instead, which the LAN web console and the serial
// `show` command both dump without needing the server to be reachable at all.
esp_reset_reason_t bootResetReason = ESP_RST_UNKNOWN;
uint32_t bootAtMs = 0;
uint16_t wifiDropCount = 0;          // confirmed outages since boot (post grace period)
uint32_t lastWifiDropAt = 0;         // millis() of the most recent confirmed outage
uint32_t lastWifiRecoveredAt = 0;    // millis() when it came back
uint32_t longestWifiOutageMs = 0;    // worst confirmed outage this boot
enum class ServerLinkState : uint8_t { UNKNOWN, WIFI_DOWN, UNREACHABLE, AVAILABLE };
ServerLinkState serverLinkState = ServerLinkState::UNKNOWN;
uint8_t serverSuccessStreak = 0, serverFailureStreak = 0;
bool suppressNetworkUiErrors = false;
uint16_t workerCacheCapacity = MAX_CACHED_WORKERS;
uint16_t operationCacheCapacity = MAX_CACHED_OPERATIONS;
uint16_t offlineSessionCapacity = MAX_OFFLINE_SESSIONS;
uint16_t offlineAckCapacity = MAX_OFFLINE_EVENTS;

// TEMP DIAGNOSTIC (2026-08-22, round 3): serverLinkState degrading toward
// UNREACHABLE with offlineMode stuck at 1 means something is actually
// failing in the background (heartbeat and/or the offline-event sync POST
// both run regardless of offlineMode/user action), but whichever setError()
// fires LAST always overwrites rt.lastHttpStatus/rt.lastError before anyone
// can look at the screen -- so the real failing status gets clobbered by the
// time an operator sees anything. Remember it separately, tagged by source.
int lastLinkFailureStatus = 0;
char lastLinkFailureSource[16] = "";

void recordServerResult(bool success,bool wifiDown=false,int status=0,const char* source=nullptr){
  if(success){serverFailureStreak=0;if(serverSuccessStreak<2)serverSuccessStreak++;if(serverSuccessStreak>=2)serverLinkState=ServerLinkState::AVAILABLE;}
  else{serverSuccessStreak=0;if(wifiDown){serverFailureStreak=2;serverLinkState=ServerLinkState::WIFI_DOWN;}
    else{if(serverFailureStreak<2)serverFailureStreak++;if(serverFailureStreak>=2)serverLinkState=ServerLinkState::UNREACHABLE;}
    lastLinkFailureStatus=status;
    if(source) safeCopy(lastLinkFailureSource,sizeof(lastLinkFailureSource),source);
  }
  rt.online=serverLinkState==ServerLinkState::AVAILABLE;
  if (!rt.online) otaLinkReady = false;
}

static void freeOfflineBuffers() {
  if (workerCache) heap_caps_free(workerCache);
  if (operationCache) heap_caps_free(operationCache);
  if (offlineSessions) heap_caps_free(offlineSessions);
  if (offlineAckScratch) heap_caps_free(offlineAckScratch);
  workerCache = nullptr; operationCache = nullptr;
  offlineSessions = nullptr; offlineAckScratch = nullptr;
  offlineBuffersReady = false;
}

bool allocateOfflineBuffers() {
  if (offlineBuffersReady) return true;
  freeOfflineBuffers();

  const bool usePsram = psramFound() && ESP.getFreePsram() > 256U * 1024U;
  uint32_t caps = usePsram ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                           : (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  // Without PSRAM keep a smaller but useful cache so HTTP/JSON still has RAM.
  workerCacheCapacity = usePsram ? MAX_CACHED_WORKERS : 80;
  operationCacheCapacity = usePsram ? MAX_CACHED_OPERATIONS : 120;
  offlineSessionCapacity = usePsram ? MAX_OFFLINE_SESSIONS : 16;
  offlineAckCapacity = usePsram ? MAX_OFFLINE_EVENTS : 64;

  workerCache = static_cast<CachedWorker*>(heap_caps_calloc(
      workerCacheCapacity, sizeof(CachedWorker), caps));
  operationCache = static_cast<CachedOperation*>(heap_caps_calloc(
      operationCacheCapacity, sizeof(CachedOperation), caps));
  offlineSessions = static_cast<OfflineSession*>(heap_caps_calloc(
      offlineSessionCapacity, sizeof(OfflineSession), caps));
  offlineAckScratch = static_cast<char (*)[64]>(heap_caps_calloc(
      offlineAckCapacity, 64, caps));

  offlineBuffersReady = workerCache && operationCache && offlineSessions && offlineAckScratch;
  if (!offlineBuffersReady) {
    freeOfflineBuffers();
    Serial.printf("[MEM] Buffer allocation failed mode=%s heap=%u psram=%u\n",
                  usePsram ? "PSRAM" : "HEAP",
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getFreePsram()));
    return false;
  }

  Serial.printf("[MEM] Buffers mode=%s workers=%u ops=%u sessions=%u ack=%u heap=%u psram=%u\n",
                usePsram ? "PSRAM" : "HEAP", workerCacheCapacity,
                operationCacheCapacity, offlineSessionCapacity, offlineAckCapacity,
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getFreePsram()));
  return true;
}

uint32_t objectChecksum(const void* ptr, size_t len, size_t checksumOffset) {
  uint8_t temp[sizeof(OfflineLogRecord)];
  if (len > sizeof(temp)) return 0;
  memcpy(temp, ptr, len);
  memset(temp + checksumOffset, 0, sizeof(uint32_t));
  return fnv1a32(temp, len);
}

template<typename T> bool validObject(const T& obj) {
  return obj.magic == OFF_MAGIC && obj.checksum == objectChecksum(&obj, sizeof(T), offsetof(T, checksum));
}
template<typename T> void sealObject(T& obj) {
  obj.magic = OFF_MAGIC;
  obj.checksum = objectChecksum(&obj, sizeof(T), offsetof(T, checksum));
}

uint32_t currentEpoch() {
  time_t now = time(nullptr);
  return now > 1700000000 ? static_cast<uint32_t>(now) : 0;
}
uint32_t nextDeviceSequence() {
  prefs.begin("mesflow", false);
  uint32_t seq = prefs.getUInt("event_seq", 0) + 1;
  prefs.putUInt("event_seq", seq);
  prefs.end();
  return seq;
}

bool atomicWrite(const char* path, const uint8_t* data, size_t len) {
  String tmp = String(path) + ".tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  size_t n = f.write(data, len); f.flush(); f.close();
  if (n != len) { LittleFS.remove(tmp); return false; }
  LittleFS.remove(path);
  return LittleFS.rename(tmp, path);
}

template<typename T> uint16_t loadArray(const char* path, T* dst, uint16_t maxCount) {
  File f = LittleFS.open(path, "r"); if (!f) return 0;
  uint16_t count = 0; T item;
  while (count < maxCount && f.read(reinterpret_cast<uint8_t*>(&item), sizeof(T)) == sizeof(T)) {
    if (validObject(item)) dst[count++] = item;
  }
  f.close(); return count;
}
template<typename T> bool saveArray(const char* path, T* src, uint16_t count) {
  return atomicWrite(path, reinterpret_cast<const uint8_t*>(src), sizeof(T) * count);
}

void loadOfflineStorage() {
  if (!fsReady || !offlineBuffersReady) return;
  workerCacheCount = loadArray(WORKER_CACHE_FILE, workerCache, workerCacheCapacity);
  operationCacheCount = loadArray(OP_CACHE_FILE, operationCache, operationCacheCapacity);
  offlineSessionCount = loadArray(SESSION_FILE, offlineSessions, offlineSessionCapacity);
  Serial.printf("[OFFLINE] cache workers=%u ops=%u sessions=%u\n", workerCacheCount, operationCacheCount, offlineSessionCount);
}

void cacheWorkerNow(const char* qr, const char* code, const char* name) {
  if (!fsReady || !offlineBuffersReady || !qr || !qr[0]) return;
  int idx = -1; for (uint16_t i=0;i<workerCacheCount;i++) if (!strcmp(workerCache[i].qr,qr)) { idx=i; break; }
  if (idx < 0) { if (workerCacheCount >= workerCacheCapacity) idx = 0; else idx = workerCacheCount++; }
  CachedWorker &w=workerCache[idx]; memset(&w,0,sizeof(w));
  safeCopy(w.qr,sizeof(w.qr),qr); safeCopy(w.code,sizeof(w.code),code); safeCopy(w.name,sizeof(w.name),name);
  w.cachedEpoch=currentEpoch(); if(!w.cachedEpoch) w.cachedEpoch=1; sealObject(w); saveArray(WORKER_CACHE_FILE,workerCache,workerCacheCount);
}
void cacheOperationNow(const char* qr, const char* name, const char* po, const char* part) {
  if (!fsReady || !offlineBuffersReady || !qr || !qr[0]) return;
  int idx=-1; for(uint16_t i=0;i<operationCacheCount;i++) if(!strcmp(operationCache[i].qr,qr)){idx=i;break;}
  if(idx<0){if(operationCacheCount>=operationCacheCapacity)idx=0;else idx=operationCacheCount++;}
  CachedOperation &o=operationCache[idx]; memset(&o,0,sizeof(o));
  safeCopy(o.qr,sizeof(o.qr),qr);safeCopy(o.name,sizeof(o.name),name);safeCopy(o.po,sizeof(o.po),po);safeCopy(o.part,sizeof(o.part),part);safeCopy(o.station,sizeof(o.station),STATION_CODE);
  o.cachedEpoch=currentEpoch();if(!o.cachedEpoch)o.cachedEpoch=1;sealObject(o);saveArray(OP_CACHE_FILE,operationCache,operationCacheCount);
}
CachedWorker* findCachedWorker(const char* qr){for(uint16_t i=0;i<workerCacheCount;i++)if(!strcmp(workerCache[i].qr,qr))return &workerCache[i];return nullptr;}
CachedOperation* findCachedOperation(const char* qr){for(uint16_t i=0;i<operationCacheCount;i++)if(!strcmp(operationCache[i].qr,qr)&&!strcmp(operationCache[i].station,STATION_CODE))return &operationCache[i];return nullptr;}
int findOfflineSession(const char* workerQr){for(uint16_t i=0;i<offlineSessionCount;i++)if(!strcmp(offlineSessions[i].workerQr,workerQr))return i;return -1;}

bool saveOfflineSessions(){for(uint16_t i=0;i<offlineSessionCount;i++)sealObject(offlineSessions[i]);return saveArray(SESSION_FILE,offlineSessions,offlineSessionCount);}

uint16_t countPendingOfflineEvents() {
  if(!fsReady || !offlineBuffersReady)return 0; uint16_t ackCount=0,eventCount=0;
  File f=LittleFS.open(EVENT_LOG_FILE,"r"); if(!f)return 0; OfflineLogRecord r;
  while(f.read((uint8_t*)&r,sizeof(r))==sizeof(r)){if(!validObject(r))continue;if((r.recordType==(uint8_t)LogRecordType::ACK||r.recordType==(uint8_t)LogRecordType::REJECT)&&ackCount<offlineAckCapacity)safeCopy(offlineAckScratch[ackCount++],64,r.eventId);}
  f.seek(0);while(f.read((uint8_t*)&r,sizeof(r))==sizeof(r)){if(!validObject(r)||r.recordType!=(uint8_t)LogRecordType::EVENT)continue;bool ack=false;for(uint16_t i=0;i<ackCount;i++)if(!strcmp(offlineAckScratch[i],r.eventId)){ack=true;break;}if(!ack)eventCount++;}
  f.close();return eventCount;
}

bool appendLogRecord(OfflineLogRecord &r) {
  if(!fsReady)return false;
  const size_t total=LittleFS.totalBytes(),used=LittleFS.usedBytes();
  if(total&&used*100U/total>=95U){setError(-41,"BỘ NHỚ ĐÃ ĐẦY - BÁO QUẢN LÝ");return false;}
  if(total&&used*100U/total>=85U)Serial.printf("[OFFLINE] storage warning used=%u%%\n",(unsigned)(used*100U/total));
  sealObject(r); File f=LittleFS.open(EVENT_LOG_FILE,"a");if(!f)return false;
  size_t n=f.write((uint8_t*)&r,sizeof(r));f.flush();f.close();return n==sizeof(r);
}
bool appendAck(const char* eventId){OfflineLogRecord a;memset(&a,0,sizeof(a));a.magic=OFF_MAGIC;a.version=OFF_VERSION;a.recordType=(uint8_t)LogRecordType::ACK;safeCopy(a.eventId,sizeof(a.eventId),eventId);return appendLogRecord(a);}
bool appendReject(const char* eventId,const char* reason){OfflineLogRecord a;memset(&a,0,sizeof(a));a.magic=OFF_MAGIC;a.version=OFF_VERSION;a.recordType=(uint8_t)LogRecordType::REJECT;safeCopy(a.eventId,sizeof(a.eventId),eventId);safeCopy(a.operationName,sizeof(a.operationName),reason);return appendLogRecord(a);}

bool eventExistsInLog(const char* eventId) {
  if(!fsReady || !eventId || !eventId[0]) return false;
  File f=LittleFS.open(EVENT_LOG_FILE,"r"); if(!f)return false; OfflineLogRecord r;
  while(f.read((uint8_t*)&r,sizeof(r))==sizeof(r)) { if(validObject(r) && r.recordType==(uint8_t)LogRecordType::EVENT && !strcmp(r.eventId,eventId)){f.close();return true;} }
  f.close(); return false;
}
bool appendOfflineEventWithIdentity(OfflineEventType type,const char* eventId,uint32_t sequence,const char* localSessionId,const char* workerQr,const char* workerName,const char* operationQr,const char* operationName,int goodQty,int defectQty,uint32_t epoch,int repairableQty=0){
  if(eventExistsInLog(eventId)) return true;
  if(countPendingOfflineEvents()>=offlineAckCapacity){setError(-41,"BO NHO OFFLINE DAY");return false;}
  OfflineLogRecord e;memset(&e,0,sizeof(e));e.magic=OFF_MAGIC;e.version=OFF_VERSION;e.recordType=(uint8_t)LogRecordType::EVENT;e.eventType=(uint8_t)type;e.sequence=sequence;e.eventEpoch=epoch;e.bootId=bootId;
  safeCopy(e.eventId,sizeof(e.eventId),eventId);safeCopy(e.localSessionId,sizeof(e.localSessionId),localSessionId);safeCopy(e.workerQr,sizeof(e.workerQr),workerQr);safeCopy(e.workerName,sizeof(e.workerName),workerName);safeCopy(e.operationQr,sizeof(e.operationQr),operationQr);safeCopy(e.operationName,sizeof(e.operationName),operationName);e.goodQty=goodQty;e.defectQty=defectQty;
  // Preserve the v1 on-flash record size. FINISH does not need workerName for
  // replay (workerQr is canonical), so this field safely carries rework qty.
  if(type==OfflineEventType::FINISH) snprintf(e.workerName,sizeof(e.workerName),"RW:%d",repairableQty);
  return appendLogRecord(e);
}

bool enqueueOfflineEvent(OfflineEventType type,const char* localSessionId,int goodQty=0,int defectQty=0){
  uint32_t seq=nextDeviceSequence(); char eventId[64]; snprintf(eventId,sizeof(eventId),"%s-%010lu",DEVICE_ID,(unsigned long)seq);
  bool ok=appendOfflineEventWithIdentity(type,eventId,seq,localSessionId,rt.workerQr,rt.workerName,rt.operationQr,rt.operationName,goodQty,defectQty,currentEpoch());
  if(!ok){setError(-42,"KHONG LUU DUOC OFFLINE");return false;}
  Serial.printf("[OFFLINE] queued %s seq=%lu pending=%u\n",type==OfflineEventType::START?"START":"FINISH",(unsigned long)seq,countPendingOfflineEvents());return true;
}

bool offlineLookupWorker(const char* qr){
  CachedWorker* w=findCachedWorker(qr);if(!w){setError(-43,"THE CHUA CO CACHE");return false;}
  clearRuntimeSelection();rt.hasWorker=true;safeCopy(rt.workerQr,sizeof(rt.workerQr),qr);safeCopy(rt.workerCode,sizeof(rt.workerCode),w->code);safeCopy(rt.workerName,sizeof(rt.workerName),w->name);
  int si=findOfflineSession(qr);if(si>=0){OfflineSession&s=offlineSessions[si];rt.activeSessionId=-1;safeCopy(rt.activeGroupId,sizeof(rt.activeGroupId),s.localSessionId);safeCopy(rt.operationQr,sizeof(rt.operationQr),s.operationQr);safeCopy(rt.operationName,sizeof(rt.operationName),s.operationName);rt.hasOperation=true;demoGoodQty=0;demoReworkQty=0;demoDefectQty=0;setUi(UiState::INPUT_GOOD);}else setUi(UiState::WORKER_OK);
  return true;
}
bool offlineLookupOperationAndStart(const char* qr){
  CachedOperation* o=findCachedOperation(qr);if(!o){setError(-44,"MA CHUA CO CACHE");return false;}if(offlineSessionCount>=offlineSessionCapacity){setError(-45,"QUA NHIEU SESSION");return false;}
  rt.hasOperation=true;safeCopy(rt.operationQr,sizeof(rt.operationQr),qr);safeCopy(rt.operationName,sizeof(rt.operationName),o->name);safeCopy(rt.po,sizeof(rt.po),o->po);safeCopy(rt.part,sizeof(rt.part),o->part);setUi(UiState::OPERATION_OK);delay(OP_REVIEW_HOLD_MS);
  OfflineSession s;memset(&s,0,sizeof(s));uint32_t seq=nextDeviceSequence();snprintf(s.localSessionId,sizeof(s.localSessionId),"LS-%s-%010lu",DEVICE_ID,(unsigned long)seq);snprintf(s.startEventId,sizeof(s.startEventId),"%s-%010lu",DEVICE_ID,(unsigned long)seq);safeCopy(s.workerQr,sizeof(s.workerQr),rt.workerQr);safeCopy(s.workerName,sizeof(s.workerName),rt.workerName);safeCopy(s.operationQr,sizeof(s.operationQr),rt.operationQr);safeCopy(s.operationName,sizeof(s.operationName),rt.operationName);s.startSequence=seq;s.startEpoch=currentEpoch();sealObject(s);
  // Two-phase durability: session intent first, event log second. Boot recovery fills any missing event.
  offlineSessions[offlineSessionCount++]=s;if(!saveOfflineSessions()){offlineSessionCount--;setError(-46,"LOI LUU SESSION");return false;}
  if(!appendOfflineEventWithIdentity(OfflineEventType::START,s.startEventId,s.startSequence,s.localSessionId,s.workerQr,s.workerName,s.operationQr,s.operationName,0,0,s.startEpoch)){setError(-42,"KHONG LUU START OFFLINE");return false;}
  offlineMode=true;
  drawSimple("ĐÃ LƯU TẠM",rt.operationName,rt.workerName,"QUÉT THẺ TIẾP",C_WARN);delay(1800);resetForNextWorker();return true;
}

bool offlineFinishSession(){
  int si=findOfflineSession(rt.workerQr);if(si<0){setError(-47,"KHONG CO SESSION OFFLINE");return false;}OfflineSession &s=offlineSessions[si];
  safeCopy(rt.operationQr,sizeof(rt.operationQr),s.operationQr);safeCopy(rt.operationName,sizeof(rt.operationName),s.operationName);
  // Phase 1: persist finish intent inside session record.
  if(!s.finishPending){s.finishPending=1;s.finishSequence=nextDeviceSequence();snprintf(s.finishEventId,sizeof(s.finishEventId),"%s-%010lu",DEVICE_ID,(unsigned long)s.finishSequence);s.finishGoodQty=demoGoodQty;s.finishDefectQty=demoDefectQty;s.finishEpoch=currentEpoch();snprintf(s.workerName,sizeof(s.workerName),"RW:%d",demoReworkQty);sealObject(s);if(!saveOfflineSessions()){setError(-48,"LOI LUU FINISH");return false;}}
  // Phase 2: append immutable event. Reboot recovery repeats safely using the same event_id.
  if(!appendOfflineEventWithIdentity(OfflineEventType::FINISH,s.finishEventId,s.finishSequence,s.localSessionId,s.workerQr,s.workerName,s.operationQr,s.operationName,s.finishGoodQty,s.finishDefectQty,s.finishEpoch,demoReworkQty)){setError(-42,"KHONG LUU FINISH EVENT");return false;}
  // Phase 3: only after event is durable, remove open local session.
  for(uint16_t i=si+1;i<offlineSessionCount;i++)offlineSessions[i-1]=offlineSessions[i];offlineSessionCount--;if(!saveOfflineSessions()){setError(-48,"LOI CAP NHAT SESSION");return false;}
  drawSimple("ĐÃ LƯU TẠM",rt.workerName,"SẢN LƯỢNG ĐÃ LƯU","SẼ TỰ ĐỘNG GỬI",C_WARN);delay(1600);resetForNextWorker();return true;
}

void recoverOfflineSessionIntents(){
  if(!fsReady)return; bool changed=false;
  for(uint16_t i=0;i<offlineSessionCount;i++){
    OfflineSession &s=offlineSessions[i];
    if(s.startEventId[0] && !eventExistsInLog(s.startEventId)) appendOfflineEventWithIdentity(OfflineEventType::START,s.startEventId,s.startSequence,s.localSessionId,s.workerQr,s.workerName,s.operationQr,s.operationName,0,0,s.startEpoch);
    if(s.finishPending && s.finishEventId[0]){
      if(!eventExistsInLog(s.finishEventId)) appendOfflineEventWithIdentity(OfflineEventType::FINISH,s.finishEventId,s.finishSequence,s.localSessionId,s.workerQr,s.workerName,s.operationQr,s.operationName,s.finishGoodQty,s.finishDefectQty,s.finishEpoch,strncmp(s.workerName,"RW:",3)==0?atoi(s.workerName+3):0);
      for(uint16_t j=i+1;j<offlineSessionCount;j++)offlineSessions[j-1]=offlineSessions[j];offlineSessionCount--;i--;changed=true;
    }
  }
  if(changed)saveOfflineSessions();
}

bool readOldestPendingEvent(OfflineLogRecord &out){
  if (!fsReady || !offlineBuffersReady) return false;
  uint16_t ackCount=0;File f=LittleFS.open(EVENT_LOG_FILE,"r");if(!f)return false;OfflineLogRecord r;
  while(f.read((uint8_t*)&r,sizeof(r))==sizeof(r))if(validObject(r)&&(r.recordType==(uint8_t)LogRecordType::ACK||r.recordType==(uint8_t)LogRecordType::REJECT)&&ackCount<offlineAckCapacity)safeCopy(offlineAckScratch[ackCount++],64,r.eventId);
  f.seek(0);while(f.read((uint8_t*)&r,sizeof(r))==sizeof(r)){if(!validObject(r)||r.recordType!=(uint8_t)LogRecordType::EVENT)continue;bool ack=false;for(uint16_t i=0;i<ackCount;i++)if(!strcmp(offlineAckScratch[i],r.eventId)){ack=true;break;}if(!ack){out=r;f.close();return true;}}
  f.close();return false;
}

bool syncOneOfflineEvent(){
  if(!rt.bound||WiFi.status()!=WL_CONNECTED)return false;OfflineLogRecord e;if(!readOldestPendingEvent(e))return true;
  DynamicJsonDocument req(2304),resp(2048);req["device_id"]=DEVICE_ID;req["kiosk_id"]=DEVICE_ID;req["station_code"]=STATION_CODE;req["app_version"]=APP_VERSION;
  JsonArray events=req.createNestedArray("events");JsonObject x=events.createNestedObject();x["client_event_id"]=e.eventId;x["event_id"]=e.eventId;x["event_type"]=e.eventType==(uint8_t)OfflineEventType::START?"START":"FINISH";x["local_session_id"]=e.localSessionId;x["session_trace_id"]=e.localSessionId;x["worker_qr"]=e.workerQr;x["operation_qr"]=e.operationQr;x["good_qty"]=e.goodQty;x["defect_qty"]=e.defectQty;x["repairable_qty"]=(e.eventType==(uint8_t)OfflineEventType::FINISH&&strncmp(e.workerName,"RW:",3)==0)?atoi(e.workerName+3):0;x["scrap_qty"]=e.defectQty-(int)x["repairable_qty"];x["local_sequence"]=e.sequence;x["device_sequence"]=e.sequence;x["boot_id"]=String(e.bootId,HEX);x["device_uptime_ms"]=0;x["event_time_epoch"]=e.eventEpoch;x["time_quality"]=e.eventEpoch?"synced":"unknown";x["offline_snapshot_revision"]=offlineSnapshotRevision;x["offline"]=true;x["sync_status"]="pending";
  suppressNetworkUiErrors=true;
  const bool posted=httpPostJson(OFFLINE_SYNC_PATH,req,resp,true,true);
  suppressNetworkUiErrors=false;
  if(!posted)return false;
  JsonArray results=resp["results"].as<JsonArray>();if(results.isNull()||!results.size()){setError(-49,"SYNC KHONG CO ACK");return false;}
  const char* status=results[0]["status"]|"";const char* ackId=results[0]["event_id"]|"";
  if(strcmp(ackId,e.eventId)!=0){setError(-50,"ACK SAI EVENT");return false;}
  if(!strcmp(status,"rejected")){
    // BUG (found 2026-08-22, round 3): the server correctly rejects a
    // business-invalid offline START/FINISH (e.g. Operation's PO already
    // COMPLETED/CANCELLED or never Started) with status=="rejected", but
    // this branch only logged it to the Serial/USB console and returned
    // true -- "sync successful" to every caller. Nothing ever reached the
    // kiosk screen, Kiosk Events, or notifications, and since START/FINISH
    // are local-first (the operator already saw "DA LUU TAM" and moved on
    // to the next worker before this background sync runs), the rejection
    // was invisible everywhere: no work_session was ever created, so the
    // production simply never reached the dashboard with no trace of why.
    const char* reasonCode=results[0]["reason_code"]|"BUSINESS_REJECT";
    const char* reasonDetail=results[0]["reason"]|"";
    const bool isStart=e.eventType==(uint8_t)OfflineEventType::START;
    Serial.printf("[SYNC] rejected event=%s reason=%s detail=%s\n",e.eventId,reasonCode,reasonDetail);
    char notifyMsg[224];
    snprintf(notifyMsg,sizeof(notifyMsg),"%s bi tu choi (%s): NV=%s OP=%s. Ly do: %s",
             isStart?"BAT DAU":"KET THUC",reasonCode,e.workerQr,e.operationQr,
             reasonDetail[0]?reasonDetail:reasonCode);
    // Server-side visibility for admin/supervisor (Kiosk Events + notifications --
    // severity=ERROR makes analytics.py's KioskEventRepository.ingest() also
    // create an admin notification row, unlike the RECONCILE_REPLAY-only
    // Session Exceptions path which never covers an ordinary offline reject).
    sendKioskEvent("OFFLINE_SYNC_REJECTED","ERROR",notifyMsg,"Kiem tra OP/PO va bao quan doc",0);
    if(!appendReject(e.eventId,reasonCode)){setError(-52,"KHONG LUU DUOC REJECT");return false;}
    lastOfflineSyncEpoch=currentEpoch();
    // On-device visibility for whoever is at the kiosk right now. Short,
    // no-diacritic text -- same convention as every other setError() call
    // in this file -- rather than the raw (possibly long, diacritic) server
    // reason, which is already sent in full via sendKioskEvent above.
    setError(0,isStart?"BI TU CHOI - PO CHUA/DA XONG":"BI TU CHOI - BAO QUAN DOC");
    return true;
  }
  if(strcmp(status,"accepted")&&strcmp(status,"duplicate")){setError(-51,"SERVER TAM THOI TU CHOI");return false;}
  if(!appendAck(e.eventId)){setError(-52,"KHONG LUU DUOC ACK");return false;}
  lastOfflineSyncEpoch=currentEpoch();Serial.printf("[OFFLINE SYNC] %s -> %s, pending=%u\n",e.eventId,status,countPendingOfflineEvents());return true;
}

bool convertPendingTransactionToOfflineQueue(){
  if(!hasPendingTransaction()||!fsReady||!offlineBuffersReady)return false;
  const PendingType type=static_cast<PendingType>(pendingTx.type);
  const uint32_t seq=nextDeviceSequence(),epoch=currentEpoch();
  if(type==PendingType::START){
    if(offlineSessionCount>=offlineSessionCapacity)return false;
    OfflineSession s;memset(&s,0,sizeof(s));safeCopy(s.localSessionId,sizeof(s.localSessionId),pendingTx.token);
    safeCopy(s.startEventId,sizeof(s.startEventId),pendingTx.token);safeCopy(s.workerQr,sizeof(s.workerQr),pendingTx.workerQr);
    safeCopy(s.workerName,sizeof(s.workerName),pendingTx.workerName);safeCopy(s.operationQr,sizeof(s.operationQr),pendingTx.operationQr);
    safeCopy(s.operationName,sizeof(s.operationName),pendingTx.operationName);s.startSequence=seq;s.startEpoch=epoch;sealObject(s);
    offlineSessions[offlineSessionCount++]=s;
    if(!saveOfflineSessions()){offlineSessionCount--;return false;}
    if(!appendOfflineEventWithIdentity(OfflineEventType::START,pendingTx.token,seq,pendingTx.token,pendingTx.workerQr,pendingTx.workerName,pendingTx.operationQr,pendingTx.operationName,0,0,epoch))return false;
  }else if(type==PendingType::FINISH){
    char localId[56];snprintf(localId,sizeof(localId),"SERVER:%ld",(long)pendingTx.sessionId);
    const int rework=pendingTx.reserved==1?(int)pendingTx.createdUptime:0;
    if(!appendOfflineEventWithIdentity(OfflineEventType::FINISH,pendingTx.token,seq,localId,pendingTx.workerQr,pendingTx.workerName,pendingTx.operationQr,pendingTx.operationName,pendingTx.goodQty,pendingTx.defectQty,epoch,rework))return false;
  }else return false;
  Serial.printf("[OFFLINE] durable handoff token=%s pending=%u\n",pendingTx.token,countPendingOfflineEvents());
  clearPendingTransaction();offlineMode=true;
  drawSimple("ĐÃ LƯU TẠM","SẼ TỰ ĐỘNG","ĐỒNG BỘ","",C_WARN);delay(1400);resetForNextWorker();
  return true;
}

// ============================================================
// Utility
// ============================================================
void safeCopy(char* dst, size_t size, const char* src) {
  if (!dst || size == 0) return;
  strlcpy(dst, src ? src : "", size);
}

void clearRuntimeSelection() {
  rt.hasWorker = false;
  rt.hasOperation = false;
  rt.workerId = 0;
  rt.workerQr[0] = '\0';
  rt.workerCode[0] = '\0';
  rt.workerName[0] = '\0';
  rt.operationId = 0;
  rt.operationQr[0] = '\0';
  rt.operationCode[0] = '\0';
  rt.operationName[0] = '\0';
  rt.po[0] = '\0';
  rt.part[0] = '\0';
  rt.activeSessionId = 0;
  rt.activeGroupId[0] = '\0';
  rt.activeStartTime[0] = '\0';
  rt.batchToken[0] = '\0';
  rt.finishToken[0] = '\0';
}

void makeToken(const char* prefix, char* out, size_t outSize) {
  const uint64_t chip = ESP.getEfuseMac();
  snprintf(out, outSize, "%s-%s-%08lX-%lu",
           prefix,
           DEVICE_ID,
           static_cast<unsigned long>(chip & 0xFFFFFFFFULL),
           static_cast<unsigned long>(millis()));
}

const char* stateName(UiState s) {
  switch (s) {
    case UiState::BOOT: return "BOOT";
    case UiState::WIFI: return "WIFI";
    case UiState::BINDING: return "BINDING";
    case UiState::READY: return "READY";
    case UiState::SIM_SCAN_WORKER: return "SIM_SCAN_WORKER";
    case UiState::LOOKUP_WORKER: return "LOOKUP_WORKER";
    case UiState::WORKER_OK: return "WORKER_OK";
    case UiState::SIM_SCAN_OPERATION: return "SIM_SCAN_OPERATION";
    case UiState::LOOKUP_OPERATION: return "LOOKUP_OPERATION";
    case UiState::OPERATION_OK: return "OPERATION_OK";
    case UiState::STARTING: return "STARTING";
    case UiState::START_SUCCESS: return "START_SUCCESS";
    case UiState::ACTIVE_SESSION: return "ACTIVE_SESSION";
    case UiState::INPUT_GOOD: return "INPUT_GOOD";
    case UiState::INPUT_DEFECT: return "INPUT_DEFECT";
    case UiState::ASK_REWORK: return "ASK_REWORK";
    case UiState::INPUT_REWORK: return "INPUT_REWORK";
    case UiState::CONFIRM_QTY: return "CONFIRM_QTY";
    case UiState::FINISHING: return "FINISHING";
    case UiState::FINISH_RETRY: return "FINISH_RETRY";
    case UiState::FINISH_SUCCESS: return "FINISH_SUCCESS";
    case UiState::ERROR_STATE: return "ERROR";
    case UiState::OFFLINE: return "OFFLINE";
    case UiState::SYNC_PENDING: return "SYNC_PENDING";
    case UiState::TOUCH_TEST: return "TOUCH_TEST";
  }
  return "UNKNOWN";
}

// ============================================================
// Chuyen UTF-8 tieng Viet sang ASCII truoc khi hien thi LCD.
// Font mac dinh cua Adafruit_GFX khong ho tro Unicode.
// Du lieu gui API van giu nguyen, chi chuoi hien thi moi bo dau.
// ============================================================
String toVietnameseAscii(const char* input) {
  String s = input ? String(input) : String("");

  const char* groups[][2] = {
    {"àáạảãâầấậẩẫăằắặẳẵ", "a"}, {"ÀÁẠẢÃÂẦẤẬẨẪĂẰẮẶẲẴ", "A"},
    {"èéẹẻẽêềếệểễ", "e"},       {"ÈÉẸẺẼÊỀẾỆỂỄ", "E"},
    {"ìíịỉĩ", "i"},             {"ÌÍỊỈĨ", "I"},
    {"òóọỏõôồốộổỗơờớợởỡ", "o"}, {"ÒÓỌỎÕÔỒỐỘỔỖƠỜỚỢỞỠ", "O"},
    {"ùúụủũưừứựửữ", "u"},       {"ÙÚỤỦŨƯỪỨỰỬỮ", "U"},
    {"ỳýỵỷỹ", "y"},             {"ỲÝỴỶỸ", "Y"},
    {"đ", "d"},                 {"Đ", "D"}
  };

  for (auto &group : groups) {
    String chars = group[0];
    const char repl = group[1][0];
    for (unsigned int i = 0; i < chars.length();) {
      uint8_t lead = static_cast<uint8_t>(chars[i]);
      int n = (lead < 0x80) ? 1 : ((lead & 0xE0) == 0xC0 ? 2 : ((lead & 0xF0) == 0xE0 ? 3 : 4));
      String ch = chars.substring(i, i + n);
      s.replace(ch, String(repl));
      i += n;
    }
  }
  return s;
}

// ============================================================
// UI primitives - giao dien toi gian, chu dam gia lap
// ============================================================
static const MesflowFont* uiFont(uint8_t size) {
  if (size <= 1) return &MF_FONT_12;
  if (size == 2) return &MF_FONT_16;
  return &MF_FONT_24;
}

static uint32_t nextUtf8(const char*& cursor) {
  const uint8_t a = static_cast<uint8_t>(*cursor++);
  if (a < 0x80) return a;
  if ((a & 0xE0) == 0xC0) {
    const uint8_t b = static_cast<uint8_t>(*cursor++);
    return ((a & 0x1F) << 6) | (b & 0x3F);
  }
  if ((a & 0xF0) == 0xE0) {
    const uint8_t b = static_cast<uint8_t>(*cursor++), c = static_cast<uint8_t>(*cursor++);
    return ((a & 0x0F) << 12) | ((b & 0x3F) << 6) | (c & 0x3F);
  }
  if ((a & 0xF8) == 0xF0) {
    const uint8_t b = static_cast<uint8_t>(*cursor++), c = static_cast<uint8_t>(*cursor++), d = static_cast<uint8_t>(*cursor++);
    return ((a & 7) << 18) | ((b & 0x3F) << 12) | ((c & 0x3F) << 6) | (d & 0x3F);
  }
  return 0xFFFD;
}

static bool findUiGlyph(const MesflowFont* font, uint32_t codepoint, MesflowGlyph& glyph) {
  int lo = 0, hi = font->glyphCount - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    MesflowGlyph candidate; memcpy_P(&candidate, font->glyphs + mid, sizeof(candidate));
    if (candidate.codepoint == codepoint) { glyph = candidate; return true; }
    if (candidate.codepoint < codepoint) lo = mid + 1; else hi = mid - 1;
  }
#if MESFLOW_UI_SCREENSHOT
  Serial.printf("[UI FONT] Missing glyph U+%04lX\n", static_cast<unsigned long>(codepoint));
#endif
  return false;
}

static int16_t measureUiText(const char* text, uint8_t size) {
  if (!text) return 0;
  if (size >= 4) return strlen(text) * 6 * size; // quantity path is ASCII digits only
  const MesflowFont* font = uiFont(size); int16_t width = 0;
  const char* cursor = text;
  while (*cursor) {
    MesflowGlyph glyph; const uint32_t cp = nextUtf8(cursor);
    width += findUiGlyph(font, cp, glyph) ? glyph.advance : font->ascent / 2;
  }
  return width;
}

static void drawUiGlyph(int16_t x, int16_t baseline, const MesflowFont* font,
                        const MesflowGlyph& glyph, uint16_t color) {
  uint32_t bit = 0;
  for (uint8_t yy = 0; yy < glyph.height; ++yy) {
    for (uint8_t xx = 0; xx < glyph.width; ++xx, ++bit) {
      const uint8_t value = pgm_read_byte(font->bitmap + glyph.bitmapOffset + bit / 8);
      if (value & (0x80 >> (bit & 7))) tft.drawPixel(x + glyph.xOffset + xx, baseline + glyph.yOffset + yy, color);
    }
  }
}

void printText(int16_t x, int16_t y, const char* text, uint8_t size = 1,
               uint16_t color = C_TEXT, bool bold = false) {
  (void)bold; // subset assets use Arial Bold consistently
  if (!text) return;
  if (size >= 4) {
    tft.setTextWrap(false); tft.setTextSize(size); tft.setTextColor(color); tft.setCursor(x, y); tft.print(text);
    return;
  }
  const MesflowFont* font = uiFont(size); const int16_t baseline = y + font->ascent;
  const char* cursor = text;
  while (*cursor) {
    MesflowGlyph glyph; const uint32_t cp = nextUtf8(cursor);
    if (findUiGlyph(font, cp, glyph)) { drawUiGlyph(x, baseline, font, glyph, color); x += glyph.advance; }
    else x += font->ascent / 2;
  }
}

void printCentered(const char* text, int16_t centerX, int16_t y, uint8_t size,
                   uint16_t color, bool bold = false) {
  const int16_t width = measureUiText(text, size);
  printText(centerX - width / 2, y, text, size, color, bold);
}

void drawLine(int16_t y) {
  // Neutral separator; avoids turning the whole screen into a color block.
  tft.drawFastHLine(12, y, SW - 24, C_PANEL_2);
}

void printCenteredFit(const char* text, int16_t y, uint8_t preferredSize,
                      uint16_t color = C_TEXT, bool bold = true) {
  uint8_t size = preferredSize;
  while (size > 1 && measureUiText(text, size) > SW - 20) --size;
  printCentered(text, SW / 2, y, size, color, bold);
}

enum class TextAlign : uint8_t { LEFT, CENTER };

void drawTextBox(const char* text, int16_t x, int16_t y, int16_t width, int16_t height,
                 uint8_t maxSize, uint8_t minSize, uint8_t maxLines,
                 TextAlign align = TextAlign::CENTER, uint16_t color = C_TEXT,
                 bool bold = false) {
  if (!text || !text[0] || width <= 0 || height <= 0 || maxLines == 0) return;
  String source(text); source.replace("\n", " "); source.trim();
  for (int size = maxSize; size >= minSize; --size) {
    const int16_t lineH = size >= 4 ? 8 * size + 3 : uiFont(size)->lineHeight;
    const uint8_t allowed = min<uint8_t>(min<uint8_t>(maxLines, 4), height / lineH);
    if (allowed == 0) continue;
    String lines[4]; uint8_t count = 0; int pos = 0;
    while (pos < (int)source.length() && count < allowed) {
      while (pos < (int)source.length() && source[pos] == ' ') ++pos;
      int end = source.indexOf(' ', pos); if (end < 0) end = source.length();
      String word = source.substring(pos, end);
      String candidate = lines[count];
      if (candidate.length()) candidate += ' ';
      candidate += word;
      if (measureUiText(candidate.c_str(), size) <= width) {
        lines[count] = candidate; pos = end;
        if (pos >= (int)source.length()) ++count;
      } else if (lines[count].length()) {
        ++count;
      } else {
        // One unbroken token (PO/hostname) is shortened only at UTF-8 codepoint boundaries.
        const char* begin = word.c_str(); const char* cursor = begin; String fitted;
        while (*cursor) {
          const char* next = cursor; nextUtf8(next);
          String trial = fitted + word.substring(cursor - begin, next - begin);
          if (measureUiText((trial + "...").c_str(), size) > width) break;
          fitted = trial; cursor = next;
        }
        lines[count++] = fitted + "..."; pos = end;
      }
    }
    const bool truncated = pos < (int)source.length();
    if (truncated && count) {
      String base = lines[count - 1];
      while (base.length() && measureUiText((base + "...").c_str(), size) > width) {
        int cut = base.length() - 1;
        while (cut > 0 && (static_cast<uint8_t>(base[cut]) & 0xC0) == 0x80) --cut;
        base.remove(cut); base.trim();
      }
      lines[count - 1] = base + "...";
    }
    if (!truncated || size == minSize) {
      int16_t yy = y + max<int16_t>(0, (height - count * lineH) / 2);
      for (uint8_t i = 0; i < count; ++i) {
        int16_t xx = x;
        if (align == TextAlign::CENTER) xx += max<int16_t>(0, (width - measureUiText(lines[i].c_str(), size)) / 2);
        printText(xx, yy, lines[i].c_str(), size, color, bold); yy += lineH;
      }
      return;
    }
  }
}

void drawCenteredTextFit(const char* text, int16_t x, int16_t y, int16_t width, int16_t height,
                         uint8_t maxSize, uint8_t minSize, uint8_t maxLines,
                         uint16_t color = C_TEXT, bool bold = false) {
  drawTextBox(text, x, y, width, height, maxSize, minSize, maxLines, TextAlign::CENTER, color, bold);
}

void drawPageTitle(const char* title, uint16_t color = C_TEXT) {
  drawCenteredTextFit(title, SCREEN_LEFT_MARGIN, TITLE_Y,
                      SW - SCREEN_LEFT_MARGIN - SCREEN_RIGHT_MARGIN, 58,
                      FONT_TITLE, FONT_TITLE - 1, 2, color, true);
}

void drawSectionLabel(const char* label) {
  drawTextBox(label, SCREEN_LEFT_MARGIN, 44,
              SW - SCREEN_LEFT_MARGIN - SCREEN_RIGHT_MARGIN, 24,
              FONT_HEADER, FONT_HEADER, 1, TextAlign::LEFT, C_MUTED, true);
}

void drawOptionRow(const char* number, const char* label, int16_t y) {
  constexpr int16_t NUMBER_X = 24;
  constexpr int16_t NUMBER_W = 24;
  constexpr int16_t TEXT_X = 58;
  constexpr int16_t TEXT_W = SW - TEXT_X - SCREEN_RIGHT_MARGIN;
  drawCenteredTextFit(number, NUMBER_X, y, NUMBER_W, 28,
                      FONT_OPTION, FONT_OPTION, 1, C_INFO, true);
  drawTextBox(label, TEXT_X, y, TEXT_W, 28,
              FONT_OPTION, FONT_OPTION, 1, TextAlign::LEFT, C_TEXT, true);
}

void drawKeyValueRow(const char* label, int value, int16_t y) {
  constexpr int16_t LABEL_X = 20;
  constexpr int16_t LABEL_W = 118;
  constexpr int16_t VALUE_X = 146;
  constexpr int16_t VALUE_W = 76;
  char valueText[16]; snprintf(valueText, sizeof(valueText), "%d", value);
  drawTextBox(label, LABEL_X, y, LABEL_W, 26,
              FONT_SECTION, FONT_SECTION, 1, TextAlign::LEFT, C_TEXT);
  drawCenteredTextFit(valueText, VALUE_X, y, VALUE_W, 26,
                      FONT_SECTION, FONT_SECTION, 1, C_TEXT, true);
}

void drawFooterTwoActions(const char* left, const char* right) {
  tft.fillRect(0, FOOTER_Y, SW, FOOTER_H, C_BG);
  tft.drawFastHLine(UI_MARGIN, FOOTER_Y, SW - UI_MARGIN * 2, C_PANEL_2);
  drawCenteredTextFit(left, 0, FOOTER_Y + 10, SW / 2, 28,
                      FONT_FOOTER, FONT_FOOTER, 1, C_TEXT, true);
  drawCenteredTextFit(right, SW / 2, FOOTER_Y + 10, SW / 2, 28,
                      FONT_FOOTER, FONT_FOOTER, 1, C_TEXT, true);
}

void drawFooterBackOnly(const char* label) {
  drawFooterTwoActions(label, "");
}

void drawFooter(const char* left, const char* right = "") {
  if (right && right[0]) drawFooterTwoActions(left, right);
  else drawFooterBackOnly(left);
}

void syncClockFromServer(uint32_t serverEpoch) {
  if (serverEpoch < static_cast<uint32_t>(MIN_VALID_EPOCH)) return;

  timeval tv;
  tv.tv_sec = static_cast<time_t>(serverEpoch);
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  lastClockMinuteKey = -1;
  Serial.printf("[clock] Dong bo server_epoch=%lu\n",
                static_cast<unsigned long>(serverEpoch));
}

void drawTopClock(bool force) {
  if (!force && millis() - lastClockCheckAt < 1000UL) return;
  lastClockCheckAt = millis();

  time_t now = time(nullptr);
  bool valid = now >= MIN_VALID_EPOCH;
  struct tm localTime = {};
  int minuteKey = -1;
  char text[6] = "--:--";

  if (valid) {
    localtime_r(&now, &localTime);
    minuteKey = localTime.tm_yday * 1440 + localTime.tm_hour * 60 + localTime.tm_min;
    snprintf(text, sizeof(text), "%02d:%02d", localTime.tm_hour, localTime.tm_min);
  }

  if (!force && minuteKey == lastClockMinuteKey) return;
  lastClockMinuteKey = minuteKey;

  tft.fillRect(CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H, C_BG);
  tft.setTextWrap(false);
  tft.setTextSize(FONT_HEADER);
  tft.setTextColor(valid ? C_INFO : C_MUTED, C_BG);
  tft.setCursor(CLOCK_X + 16, CLOCK_Y + 7);
  tft.print(text);
}

void serviceNetworkIndicator(bool force) {
  if (uiState == UiState::TOUCH_TEST) return;
  const bool wifi = WiFi.status() == WL_CONNECTED;
  int8_t bars = 0;
  if (wifi) {
    const int rssi = WiFi.RSSI();
    bars = rssi >= -60 ? 3 : (rssi >= -75 ? 2 : 1);
  }
  const uint8_t link = static_cast<uint8_t>(serverLinkState);
  if (!force && bars == lastWifiBars && link == lastDrawnServerState) return;
  lastWifiBars = bars;
  lastDrawnServerState = link;
  const uint16_t color = !wifi ? C_ERR :
      (serverLinkState == ServerLinkState::AVAILABLE ? C_OK : C_WARN);
  tft.fillRect(NET_LED_X - 10, NET_LED_Y - 10, 25, 21, C_BG);
  for (int i = 0; i < 3; ++i) {
    const int16_t h = 4 + i * 4;
    const int16_t x = NET_LED_X - 8 + i * 7;
    const int16_t y = NET_LED_Y + 7 - h;
    if (i < bars) tft.fillRoundRect(x, y, 5, h, 1, color);
    else tft.drawRoundRect(x, y, 5, h, 1, C_PANEL_2);
  }
  if (!wifi) tft.drawLine(NET_LED_X - 9, NET_LED_Y - 8,
                          NET_LED_X + 10, NET_LED_Y + 8, C_ERR);
}

void drawIndustrialHeader(uint16_t statusColor);
void drawPanel(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t border);

void drawSimple(const char* title, const char* line1 = "", const char* line2 = "",
                uint16_t color = C_HEADER) {
  tft.fillScreen(C_BG);
  drawIndustrialHeader(color == C_ERR ? C_ERR : (color == C_WARN ? C_WARN : C_OK));
  drawPageTitle(title, color);
  if (line1 && line1[0]) drawCenteredTextFit(line1, SCREEN_LEFT_MARGIN, 132, 216, 48, FONT_SECTION, FONT_SECTION - 1, 2, C_TEXT, true);
  if (line2 && line2[0]) drawCenteredTextFit(line2, SCREEN_LEFT_MARGIN, 188, 216, 40, FONT_SECTION, FONT_SECTION - 1, 2, C_MUTED);
}

// Five-argument overload used by offline and synchronization screens.
void drawSimple(const char* title, const char* line1, const char* line2,
                const char* footer, uint16_t accent) {
  tft.fillScreen(C_BG);
  drawIndustrialHeader(accent == C_ERR ? C_ERR : (accent == C_WARN ? C_WARN : C_OK));
  drawPageTitle(title, accent);
  if (line1 && line1[0]) drawCenteredTextFit(line1, SCREEN_LEFT_MARGIN, 128, 216, 42, FONT_SECTION, FONT_SECTION - 1, 2, C_TEXT, true);
  if (line2 && line2[0]) drawCenteredTextFit(line2, SCREEN_LEFT_MARGIN, 180, 216, 38, FONT_SECTION, FONT_SECTION - 1, 2, C_MUTED);
  if (footer && footer[0]) drawCenteredTextFit(footer, SCREEN_LEFT_MARGIN, 230, 216, 26, FONT_FOOTER, FONT_FOOTER, 1, C_INFO, true);
}


// ============================================================
// Device configuration, phone setup portal and LAN provisioning
// ============================================================
WebServer setupWeb(80);
WebServer provisionWeb(PROVISION_HTTP_PORT);
WebServer maintenanceWeb(80);
DNSServer captiveDns;
WiFiUDP discoveryUdp;
bool setupPortalActive = false;
bool lanProvisionActive = false;
uint32_t lastDiscoveryAt = 0;
char setupApSsid[40] = "";
char provisionToken[33] = "";

// Remote diagnostic ring buffer. This intentionally stores only compact,
// high-value events so it remains safe on a kiosk that runs for months.
constexpr uint8_t REMOTE_LOG_LINES = 80;
constexpr uint16_t REMOTE_LOG_LINE_LEN = 180;
char remoteLogLines[REMOTE_LOG_LINES][REMOTE_LOG_LINE_LEN] = {};
uint8_t remoteLogHead = 0;
uint8_t remoteLogCount = 0;

static void remoteLogf(const char* format, ...) {
  char message[REMOTE_LOG_LINE_LEN - 24];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);

  char* slot = remoteLogLines[remoteLogHead];
  snprintf(slot, REMOTE_LOG_LINE_LEN, "[%10lu] %s",
           static_cast<unsigned long>(millis()), message);
  remoteLogHead = (remoteLogHead + 1) % REMOTE_LOG_LINES;
  if (remoteLogCount < REMOTE_LOG_LINES) remoteLogCount++;
}

static String remoteLogsText() {
  String out;
  out.reserve(static_cast<size_t>(remoteLogCount) * 90U + 64U);
  uint8_t first = (remoteLogHead + REMOTE_LOG_LINES - remoteLogCount) % REMOTE_LOG_LINES;
  for (uint8_t i = 0; i < remoteLogCount; ++i) {
    uint8_t index = (first + i) % REMOTE_LOG_LINES;
    out += remoteLogLines[index];
    out += '\n';
  }
  return out;
}

static void copyConfigValue(char* dst, size_t size, const String& value) {
  strlcpy(dst, value.c_str(), size);
}

static String htmlEscape(const char* value) {
  String out;
  for (const char* p = value ? value : ""; *p; ++p) {
    switch (*p) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += *p; break;
    }
  }
  return out;
}

static String chipSuffix() {
  uint64_t chip = ESP.getEfuseMac();
  char suffix[9];
  snprintf(suffix, sizeof(suffix), "%08lX", static_cast<unsigned long>(chip & 0xFFFFFFFFULL));
  return String(suffix);
}

static String buildMdnsHostname() {
  String source = String(DEVICE_ID); source.toLowerCase();
  String host = "mesflow-";
  bool lastDash = false;
  for (size_t i = 0; i < source.length() && host.length() < 55; ++i) {
    const char c = source[i];
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      host += c; lastDash = false;
    } else if (!lastDash && host.length() > 0) {
      host += '-'; lastDash = true;
    }
  }
  while (host.endsWith("-")) host.remove(host.length() - 1);
  if (host == "mesflow") host = String("mesflow-") + chipSuffix();
  return host;
}

static void refreshProvisionToken() {
  uint32_t a = esp_random();
  uint32_t b = esp_random();
  snprintf(provisionToken, sizeof(provisionToken), "%08lX%08lX",
           static_cast<unsigned long>(a), static_cast<unsigned long>(b));
}

static void randomHex(char* out, size_t bytes) {
  static const char HEX_DIGITS[] = "0123456789abcdef";
  for (size_t i = 0; i < bytes; ++i) {
    uint8_t v = static_cast<uint8_t>(esp_random() & 0xFF);
    out[i * 2] = HEX_DIGITS[v >> 4];
    out[i * 2 + 1] = HEX_DIGITS[v & 0x0F];
  }
  out[bytes * 2] = '\0';
}

bool loadOrCreateDeviceIdentity() {
  Preferences identity;
  if (!identity.begin("mf_identity", false)) {
    Serial.println("[IDENTITY] Khong mo duoc NVS namespace mf_identity (toi da 15 ky tu).");
    return false;
  }

  String uuid = identity.getString("device_uuid", "");
  String secret = identity.getString("device_secret", "");

  if (uuid.length() != 36 || secret.length() != 64) {
    char raw[33];
    randomHex(raw, 16);
    snprintf(DEVICE_UUID, sizeof(DEVICE_UUID), "%.8s-%.4s-4%.3s-a%.3s-%.12s",
             raw, raw + 8, raw + 12, raw + 16, raw + 20);
    randomHex(DEVICE_SECRET, 32);
    bool ok = identity.putString("device_uuid", DEVICE_UUID) > 0 &&
              identity.putString("device_secret", DEVICE_SECRET) > 0;
    identity.putUInt("identity_ver", 1);

    // Doc lai ngay khi namespace van dang mo de xac nhan NVS ghi thanh cong.
    String verifyUuid = identity.getString("device_uuid", "");
    String verifySecret = identity.getString("device_secret", "");
    ok = ok && verifyUuid == DEVICE_UUID && verifySecret == DEVICE_SECRET;
    identity.end();
    if (!ok) {
      DEVICE_UUID[0] = '\0'; DEVICE_SECRET[0] = '\0';
      Serial.println("[IDENTITY] Tao identity that bai.");
      return false;
    }
    Serial.printf("[IDENTITY] Da tao identity vinh vien: %s\n", DEVICE_UUID);
    return true;
  }

  copyConfigValue(DEVICE_UUID, sizeof(DEVICE_UUID), uuid);
  copyConfigValue(DEVICE_SECRET, sizeof(DEVICE_SECRET), secret);
  identity.end();
  Serial.printf("[IDENTITY] Da nap identity: %s\n", DEVICE_UUID);
  return true;
}

bool loadDeviceConfig() {
  prefs.begin("mesflow_cfg", true);
  String ssid = prefs.getString("wifi_ssid", WIFI_SSID);
  String pass = prefs.getString("wifi_pass", WIFI_PASSWORD);
  String server = prefs.getString("server", "");
  String otaAgent = prefs.getString("ota_agent", "");
  // Migrate devices that previously stored the MESFlow URL in this slot.
  // OTA control belongs to Deploy Agent; keep an explicit Deploy Agent URL
  // if one was configured, otherwise repair the legacy value automatically.
  if (otaAgent.length() == 0 || otaAgent.indexOf("/agent") < 0 ||
      (otaAgent.indexOf("mesflow.net") >= 0 && otaAgent.indexOf("deploy.mesflow.net") < 0)) {
    otaAgent = OTA_AGENT_BASE;
  }
  String deviceId = prefs.getString("device_id", DEVICE_ID);
  String deviceName = prefs.getString("device_name", DEVICE_NAME);
  String station = prefs.getString("station", STATION_CODE);
  prefs.end();

  copyConfigValue(WIFI_SSID, sizeof(WIFI_SSID), ssid);
  copyConfigValue(WIFI_PASSWORD, sizeof(WIFI_PASSWORD), pass);
  copyConfigValue(SERVER_BASE, sizeof(SERVER_BASE), server);
  copyConfigValue(OTA_AGENT_BASE, sizeof(OTA_AGENT_BASE), otaAgent);
  copyConfigValue(DEVICE_ID, sizeof(DEVICE_ID), deviceId);
  copyConfigValue(DEVICE_NAME, sizeof(DEVICE_NAME), deviceName);
  copyConfigValue(STATION_CODE, sizeof(STATION_CODE), station);
  return WIFI_SSID[0] != '\0';
}

bool saveDeviceConfig() {
  prefs.begin("mesflow_cfg", false);
  bool ok = true;
  ok &= prefs.putString("wifi_ssid", WIFI_SSID) > 0;
  prefs.putString("wifi_pass", WIFI_PASSWORD); // empty password is valid for open Wi-Fi
  if (!SERVER_BASE[0]) { prefs.end(); return false; }
  ok &= prefs.putString("server", SERVER_BASE) > 0;
  ok &= prefs.putString("ota_agent", OTA_AGENT_BASE) > 0;
  ok &= prefs.putString("device_id", DEVICE_ID) > 0;
  ok &= prefs.putString("device_name", DEVICE_NAME) > 0;
  ok &= prefs.putString("station", STATION_CODE) > 0;
  prefs.putUInt("cfg_ver", 1);
  prefs.end();
  return ok;
}

static bool validServerBase(const String& value) {
  return (value.startsWith("http://") || value.startsWith("https://")) &&
         value.length() >= 10 && value.length() < sizeof(SERVER_BASE);
}

static const char* otaAgentBase() {
  return validServerBase(String(OTA_AGENT_BASE)) ? OTA_AGENT_BASE : SERVER_BASE;
}

static bool hasServerConfig() {
  return SERVER_BASE[0] != '\0' && validServerBase(String(SERVER_BASE));
}

static String setupPage() {
  String page;
  page.reserve(7500);
  page += F("<!doctype html><html><head><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>MESFlow Kiosk Setup</title><style>");
  page += F("body{font-family:Arial,sans-serif;background:#eef2f6;margin:0;padding:18px;color:#17202a}");
  page += F(".card{max-width:620px;margin:auto;background:white;border-radius:14px;padding:20px;box-shadow:0 3px 18px #0002}");
  page += F("h1{font-size:24px;margin:0 0 6px}.sub{color:#607080;margin-bottom:18px}");
  page += F("label{display:block;font-weight:700;margin-top:14px}input,select,button{width:100%;box-sizing:border-box;padding:12px;margin-top:6px;border-radius:9px;border:1px solid #bbc5cf;font-size:16px}");
  page += F("button{background:#1769aa;color:white;border:0;font-weight:700}.secondary{background:#52616b}.row{display:grid;grid-template-columns:1fr auto;gap:8px}.status{white-space:pre-wrap;background:#f4f6f8;padding:12px;border-radius:9px;margin-top:12px}.hint{font-size:13px;color:#667}");
  page += F("</style></head><body><div class='card'><h1>MESFlow Kiosk Setup</h1>");
  page += F("<div class='sub'>Cấu hình Wi-Fi bằng điện thoại. ESP32 AP: ");
  page += htmlEscape(setupApSsid); page += F(" · IP 192.168.4.1</div>");
  page += F("<button type='button' onclick='scanWifi()'>Quét Wi-Fi gần đây</button>");
  page += F("<label>Wi-Fi SSID</label><select id='ssid'><option>");
  page += htmlEscape(WIFI_SSID); page += F("</option></select>");
  page += F("<label>Mật khẩu Wi-Fi</label><input id='pass' type='password' value='");
  page += htmlEscape(WIFI_PASSWORD); page += F("' autocomplete='new-password'>");
  page += F("<button type='button' onclick='saveCfg()'>Lưu, kiểm tra và khởi động lại</button>");
  page += F("<div id='status' class='status'>Sẵn sàng.</div></div><script>");
  page += F("async function scanWifi(){let s=document.getElementById('status');s.textContent='Đang quét...';try{let r=await fetch('/api/wifi/scan');let j=await r.json();let e=document.getElementById('ssid');e.innerHTML='';j.networks.forEach(n=>{let o=document.createElement('option');o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+' dBm)'+(n.secure?' 🔒':'');e.appendChild(o)});s.textContent='Đã tìm thấy '+j.networks.length+' mạng.'}catch(x){s.textContent='Lỗi quét Wi-Fi: '+x}}");
  page += F("async function saveCfg(){let s=document.getElementById('status');s.textContent='Đang lưu...';let b={ssid:document.getElementById('ssid').value,password:document.getElementById('pass').value};try{let r=await fetch('/api/config/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});let j=await r.json();s.textContent=j.message||JSON.stringify(j)}catch(x){s.textContent='Lỗi lưu: '+x}}");
  page += F("</script></body></html>");
  return page;
}

static void sendSetupRedirect() {
  setupWeb.sendHeader("Location", "http://192.168.4.1/", true);
  setupWeb.send(302, "text/plain", "");
}

static void handleWifiScan() {
  int count = WiFi.scanNetworks(false, true);
  DynamicJsonDocument doc(6144);
  JsonArray networks = doc.createNestedArray("networks");
  for (int i = 0; i < count && i < 30; ++i) {
    if (WiFi.SSID(i).length() == 0) continue;
    JsonObject row = networks.createNestedObject();
    row["ssid"] = WiFi.SSID(i);
    row["rssi"] = WiFi.RSSI(i);
    row["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  String body; serializeJson(doc, body);
  setupWeb.send(200, "application/json", body);
}

static bool applyConfigJson(JsonDocument& doc, bool allowWifi, String& error) {
  String ssid = doc["ssid"] | "";
  String password = doc["password"] | "";
  String server = doc["server_url"] | SERVER_BASE;
  String station = doc["station_code"] | STATION_CODE;
  String device = doc["device_id"] | DEVICE_ID;
  String name = doc["device_name"] | DEVICE_NAME;

  ssid.trim(); server.trim(); station.trim(); device.trim(); name.trim();
  if (allowWifi && ssid.length() == 0) { error = "SSID không được để trống"; return false; }
  if (!validServerBase(server)) { error = "Server URL bắt buộc và phải bắt đầu bằng http:// hoặc https://"; return false; }
  if (station.length() == 0 || station.length() >= sizeof(STATION_CODE)) { error = "Station code không hợp lệ"; return false; }
  if (device.length() == 0 || device.length() >= sizeof(DEVICE_ID)) { error = "Device ID không hợp lệ"; return false; }
  if (name.length() == 0 || name.length() >= sizeof(DEVICE_NAME)) { error = "Tên thiết bị không hợp lệ"; return false; }

  if (allowWifi) {
    copyConfigValue(WIFI_SSID, sizeof(WIFI_SSID), ssid);
    copyConfigValue(WIFI_PASSWORD, sizeof(WIFI_PASSWORD), password);
  }
  bool identityChanged = strcmp(STATION_CODE, station.c_str()) != 0 ||
                         strcmp(DEVICE_ID, device.c_str()) != 0 ||
                         strcmp(SERVER_BASE, server.c_str()) != 0;
  copyConfigValue(SERVER_BASE, sizeof(SERVER_BASE), server);
  copyConfigValue(STATION_CODE, sizeof(STATION_CODE), station);
  copyConfigValue(DEVICE_ID, sizeof(DEVICE_ID), device);
  copyConfigValue(DEVICE_NAME, sizeof(DEVICE_NAME), name);

  const char* suppliedToken = doc["kiosk_token"] | "";
  if (suppliedToken[0]) {
    strlcpy(rt.kioskToken, suppliedToken, sizeof(rt.kioskToken));
    rt.bound = true;
  } else if (identityChanged) {
    rt.kioskToken[0] = '\0';
    rt.bound = false;
  }

  if (!saveDeviceConfig()) { error = "Không ghi được cấu hình vào NVS"; return false; }
  prefs.begin("mesflow", false);
  if (rt.kioskToken[0]) prefs.putString("token", rt.kioskToken); else prefs.remove("token");
  prefs.putString("station", STATION_CODE);
  prefs.end();
  return true;
}

static void handlePhoneConfigSave() {
  DynamicJsonDocument doc(2048);
  DeserializationError parse = deserializeJson(doc, setupWeb.arg("plain"));
  DynamicJsonDocument response(512);
  if (parse) {
    response["ok"] = false; response["message"] = "JSON không hợp lệ";
    String body; serializeJson(response, body); setupWeb.send(400, "application/json", body); return;
  }
  String ssid = doc["ssid"] | "";
  String password = doc["password"] | "";
  ssid.trim();
  String error;
  if (ssid.length() == 0 || ssid.length() >= sizeof(WIFI_SSID) || password.length() >= sizeof(WIFI_PASSWORD)) {
    error = "Thông tin Wi-Fi không hợp lệ";
    response["ok"] = false; response["message"] = error;
    String body; serializeJson(response, body); setupWeb.send(400, "application/json", body); return;
  }
  safeCopy(WIFI_SSID, sizeof(WIFI_SSID), ssid.c_str());
  safeCopy(WIFI_PASSWORD, sizeof(WIFI_PASSWORD), password.c_str());
  // Must use the same "mesflow_cfg" namespace that loadDeviceConfig() reads
  // at boot. This handler used to write into the "mesflow" namespace
  // instead, so the phone setup portal would report success but the SSID
  // never actually took effect after reboot -- ESP fell back to the
  // firmware's built-in dev WiFi every time.
  prefs.begin("mesflow_cfg", false);
  const bool saved = prefs.putString("wifi_ssid", WIFI_SSID) > 0;
  prefs.putString("wifi_pass", WIFI_PASSWORD);
  prefs.end();
  if (!saved) {
    response["ok"] = false; response["message"] = "Không lưu được Wi-Fi";
    String body; serializeJson(response, body); setupWeb.send(500, "application/json", body); return;
  }
  response["ok"] = true;
  response["message"] = "Đã lưu. ESP32 sẽ khởi động lại sau 2 giây.";
  String body; serializeJson(response, body); setupWeb.send(200, "application/json", body);
  delay(1200); ESP.restart();
}

void startSetupPortal(const char* reason) {
  if (setupPortalActive) return;
  setupPortalActive = true;
  lanProvisionActive = false;
  WiFi.disconnect(true, false);
  delay(200);
  WiFi.mode(WIFI_AP_STA);
  snprintf(setupApSsid, sizeof(setupApSsid), "MESFlow-Setup-%s", chipSuffix().substring(4).c_str());
  WiFi.softAP(setupApSsid, SETUP_AP_PASSWORD);
  captiveDns.start(DNS_PORT, "*", WiFi.softAPIP());

  setupWeb.on("/", HTTP_GET, [](){ setupWeb.send(200, "text/html; charset=utf-8", setupPage()); });
  setupWeb.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  setupWeb.on("/api/config/save", HTTP_POST, handlePhoneConfigSave);
  setupWeb.on("/generate_204", HTTP_ANY, sendSetupRedirect);
  setupWeb.on("/hotspot-detect.html", HTTP_ANY, sendSetupRedirect);
  setupWeb.on("/connecttest.txt", HTTP_ANY, sendSetupRedirect);
  setupWeb.onNotFound(sendSetupRedirect);
  setupWeb.begin();

  drawSimple("CÀI ĐẶT WI-FI", setupApSsid, "MỞ 192.168.4.1", "", C_INFO);
  Serial.printf("[SETUP PORTAL] reason=%s SSID=%s IP=%s\n", reason ? reason : "", setupApSsid,
                WiFi.softAPIP().toString().c_str());
}

static bool provisionAuthorized() {
  return provisionWeb.hasHeader("X-Provision-Token") &&
         provisionWeb.header("X-Provision-Token").equalsIgnoreCase(provisionToken);
}

static void sendDeviceInfo(WebServer& web) {
  DynamicJsonDocument doc(1536);
  doc["type"] = "mesflow_kiosk";
  doc["device_id"] = DEVICE_ID;
  doc["device_uuid"] = DEVICE_UUID;
  doc["device_name"] = DEVICE_NAME;
  doc["station_code"] = STATION_CODE;
  doc["firmware"] = APP_VERSION;
  doc["mac"] = WiFi.macAddress();
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["server_url"] = SERVER_BASE;
  doc["configured"] = rt.bound;
  doc["pending_offline_events"] = countPendingOfflineEvents();
  doc["provision_token"] = provisionToken;
  String body; serializeJson(doc, body);
  web.send(200, "application/json", body);
}

static void handleLanProvision() {
  DynamicJsonDocument response(768);
  if (!provisionAuthorized()) {
    response["ok"] = false; response["message"] = "Provision token không hợp lệ";
    String body; serializeJson(response, body); provisionWeb.send(403, "application/json", body); return;
  }
  DynamicJsonDocument doc(2048);
  DeserializationError parse = deserializeJson(doc, provisionWeb.arg("plain"));
  if (parse) {
    response["ok"] = false; response["message"] = "JSON không hợp lệ";
    String body; serializeJson(response, body); provisionWeb.send(400, "application/json", body); return;
  }
  String error;
  if (!applyConfigJson(doc, false, error)) {
    response["ok"] = false; response["message"] = error;
    String body; serializeJson(response, body); provisionWeb.send(400, "application/json", body); return;
  }
  response["ok"] = true;
  response["device_id"] = DEVICE_ID;
  response["restart_required"] = true;
  response["message"] = "Đã lưu cấu hình từ MESFlow server";
  String body; serializeJson(response, body); provisionWeb.send(200, "application/json", body);
  delay(800); ESP.restart();
}


// Forward declarations for commands implemented later in this translation unit.
static void clearNamespace(const char* name);
static void forceSetupOnNextBoot();
static void consoleClearToken();
static void consoleClearCache();
static void consoleClearOffline();
static void clearKeypadCalibration();
bool bindKiosk();
bool sendHeartbeat();
bool syncPendingTransaction(bool userInitiated);

static String maskedToken(const char* token) {
  if (!token || !token[0]) return "EMPTY";
  String t(token);
  if (t.length() <= 8) return "STORED";
  return t.substring(0, 4) + "..." + t.substring(t.length() - 4);
}

// Short human-readable esp_reset_reason(). The raw enum int alone is not
// something anyone reads off a photo correctly under floor conditions.
static const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:   return "POWERON";     // cold power-up / power cut
    case ESP_RST_EXT:       return "EXT-PIN";
    case ESP_RST_SW:        return "SW";          // our own ESP.restart()
    case ESP_RST_PANIC:     return "PANIC";       // crash (exception/abort)
    case ESP_RST_INT_WDT:   return "INT-WDT";
    case ESP_RST_TASK_WDT:  return "TASK-WDT";    // setup()'s 40s safety net fired
    case ESP_RST_WDT:       return "OTHER-WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";    // supply sagged (scanner LED / WiFi TX spike)
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}

static String buildRemoteStatusText() {
  String out;
  out.reserve(1600);
  out += "================ MESFLOW STATUS ================\n";
  out += "Firmware : " + String(APP_VERSION) + "\n";
  // See the FORENSICS block near wifiDropCount: this is the evidence that
  // separates "the device rebooted" from "the scan just failed", and if it
  // did reboot, why.
  out += "Reset    : " + String(resetReasonName(bootResetReason)) + " (" + String((int)bootResetReason) + ")\n";
  out += "Uptime   : " + String((millis() - bootAtMs) / 1000UL) + " s\n";
  out += "WiFi drop: " + String(wifiDropCount) + " since boot; longest=" + String(longestWifiOutageMs / 1000UL) + " s";
  if (lastWifiDropAt) {
    out += "; last drop " + String((millis() - lastWifiDropAt) / 1000UL) + " s ago";
    if (lastWifiRecoveredAt >= lastWifiDropAt)
      out += ", recovered after " + String((lastWifiRecoveredAt - lastWifiDropAt) / 1000UL) + " s";
    else out += ", STILL DOWN";
  }
  out += "\n";
  out += "Device   : " + String(DEVICE_ID) + " (" + String(DEVICE_NAME) + ")\n";
  out += "Station  : " + String(STATION_CODE) + "\n";
  out += "Server   : " + String(rt.online ? "ONLINE" : "OFFLINE") + "\n";
  out += "ServerURL: " + String(SERVER_BASE[0] ? SERVER_BASE : "<NOT SET>") + "\n";
  out += "Identity: " + String(DEVICE_UUID[0] ? DEVICE_UUID : "<MISSING>") + "\n";
  out += "WiFi     : " + String(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED") + "\n";
  out += "SSID     : " + String(WIFI_SSID[0] ? WIFI_SSID : "<NOT SET>") + "\n";
  if (WiFi.status() == WL_CONNECTED) {
    out += "IP       : " + WiFi.localIP().toString() + "\n";
    out += "RSSI     : " + String(WiFi.RSSI()) + " dBm\n";
  }
  out += "Bound    : " + String(rt.bound ? "YES" : "NO") + "\n";
  out += "Online   : " + String(rt.online ? "YES" : "NO") + "\n";
  out += "Token    : " + maskedToken(rt.kioskToken) + "\n";
  out += "UI state : " + String(stateName(uiState)) + "\n";
  out += "HTTP     : " + String(rt.lastHttpStatus) + "\n";
  out += "Last err : " + String(rt.lastError[0] ? rt.lastError : "<none>") + "\n";
  out += "Pending  : " + String(hasPendingTransaction() ? "YES" : "NO") + "\n";
  out += "Touch    : " + String(touchAvailable ? "READY" : "NOT FOUND") + "\n";
  if (keypadAvailable) {
    out += "Keypad   : PCF8574T " + String(keypadMappingReady ? "READY" : "NEEDS CALIBRATION") +
           " @ 0x" + String(keypadAddress, HEX) + "\n";
  } else out += "Keypad   : NOT FOUND\n";
  out += "Queue    : " + String(countPendingOfflineEvents()) + "\n";
  out += "OfflineQ : " + String(countPendingOfflineEvents()) + "\n";
  out += "Cache    : workers=" + String(workerCacheCount) + " operations=" + String(operationCacheCount) + " sessions=" + String(offlineSessionCount) + "\n";
  out += "LittleFS : " + String(fsReady ? "READY" : "NOT READY") + "\n";
  out += "Heap     : " + String(ESP.getFreeHeap()) + " free; largest=" + String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)) + "\n";
  out += "PSRAM    : total=" + String(ESP.getPsramSize()) + " free=" + String(ESP.getFreePsram()) + " buffers=" + String(offlineBuffersReady ? "READY" : "DISABLED") + "\n";
  out += "Uptime   : " + String(millis() / 1000UL) + " sec\n";
  out += "================================================\n";
  return out;
}

static String remoteHelpText() {
  return String(
    "help | ?                 : danh sach lenh\n"
    "status | diag            : trang thai va chan doan\n"
    "config                    : cau hinh da che mat khau/token\n"
    "logs                      : log chan doan gan nhat\n"
    "bind                      : bind kiosk lai\n"
    "heartbeat                 : gui heartbeat ngay\n"
    "retry | dongbo            : thu dong bo pending transaction\n"
    "reset | r                 : ve man hinh READY\n"
    "touch-test                : mo man hinh test cam ung\n"
    "keypad-calibrate CONFIRM  : hieu chinh lai keypad ngay, khong reboot\n"
    "scan <QR/so>              : gia lap may quet hoac ban phim\n"
    "test-data                 : xem QR da cache tren ESP\n"
    "clear-token CONFIRM       : xoa kiosk token\n"
    "clear-cache CONFIRM       : xoa worker/operation cache\n"
    "clear-offline CONFIRM     : xoa queue offline (nguy hiem)\n"
    "setup CONFIRM             : mo Setup Portal\n"
    "reboot CONFIRM            : khoi dong lai\n"
    "factory-reset CONFIRM     : xoa config/runtime + LittleFS; GIU identity\n"
  );
}

static String executeRemoteCommand(String line, bool& delayedReboot, bool& openSetup) {
  delayedReboot = false;
  openSetup = false;
  line.trim();
  String cmd = line;
  cmd.toLowerCase();
  remoteLogf("WEB command: %s", cmd.c_str());

  if (cmd == "help" || cmd == "?") return remoteHelpText();
  if (cmd == "status" || cmd == "diag") return buildRemoteStatusText();
  if (cmd == "logs") return remoteLogsText();
  if (cmd == "config" || cmd == "show-config") {
    return "SSID=" + String(WIFI_SSID[0] ? WIFI_SSID : "<NOT SET>") +
           "\nserver=" + String(SERVER_BASE) +
           "\ndevice=" + String(DEVICE_ID) +
           "\nname=" + String(DEVICE_NAME) +
           "\nstation=" + String(STATION_CODE) +
           "\npassword=********\ntoken=" + maskedToken(rt.kioskToken) + "\n";
  }
  if (cmd == "test-data") {
    String out = "WORKERS (cache):\n";
    for (uint16_t i = 0; i < workerCacheCount && i < 40; ++i) {
      out += String(i + 1) + ". " + workerCache[i].qr + " | " + workerCache[i].code + " | " + workerCache[i].name + "\n";
    }
    if (!workerCacheCount) out += "<chua co cache - co the nhap QR thu cong>\n";
    out += "OPERATIONS (cache):\n";
    for (uint16_t i = 0; i < operationCacheCount && i < 80; ++i) {
      out += String(i + 1) + ". " + operationCache[i].qr + " | " + operationCache[i].name + " | PO=" + operationCache[i].po + "\n";
    }
    if (!operationCacheCount) out += "<chua co cache - co the nhap QR thu cong>\n";
    return out;
  }
  if (cmd.startsWith("scan ")) {
    String payload = line.substring(5);
    payload.trim();
    if (!payload.length()) return "Thieu QR/so. Vi du: scan WF|EMP|E001\n";
    remoteLogf("VIRTUAL SCAN: %s", payload.c_str());
    lastInputWasVirtual = true;
    lastInputWasKeypad = false;
    handleSerialLine(payload);
    return "Da gui vao kiosk nhu may quet: " + payload + "\n" + buildRemoteStatusText();
  }
  if (cmd == "touch-test" || cmd == "touch") { setUi(UiState::TOUCH_TEST); return "Da mo man hinh test cam ung.\n"; }
  if (cmd == "keypad-calibrate confirm" || cmd == "calibrate-keypad confirm") {
    if (!requestRuntimeKeypadCalibration("WEB")) {
      return "Khong the hieu chinh luc nay. Dua kiosk ve READY, dam bao khong co giao dich dang cho va thu lai.\n";
    }
    return "Da nhan lenh. Man hinh kiosk se hieu chinh keypad ngay, khong reboot.\n";
  }
  if (cmd == "bind") return String("bind => ") + (bindKiosk() ? "OK\n" : "FAIL\n") + buildRemoteStatusText();
  if (cmd == "heartbeat") return String("heartbeat => ") + (sendHeartbeat() ? "OK\n" : "FAIL\n") + buildRemoteStatusText();
  if (cmd == "retry" || cmd == "dongbo") {
    if (!hasPendingTransaction()) return "Khong co pending transaction.\n";
    bool ok = syncPendingTransaction(true);
    return String("retry => ") + (ok ? "OK\n" : "FAIL\n") + buildRemoteStatusText();
  }
  if (cmd == "reset" || cmd == "r") {
    clearRuntimeSelection(); demoGoodQty = 0; demoReworkQty = 0; demoDefectQty = 0; rt.lastError[0] = '\0'; setUi(UiState::READY);
    return "Da dua kiosk ve READY.\n";
  }
  if (cmd == "clear-token confirm") { consoleClearToken(); return "Da xoa kiosk token.\n"; }
  if (cmd == "clear-cache confirm") { consoleClearCache(); return "Da xoa cache.\n"; }
  if (cmd == "clear-offline confirm") { consoleClearOffline(); return "Da xoa queue va offline sessions.\n"; }
  if (cmd == "reboot confirm" || cmd == "restart confirm") { delayedReboot = true; return "ESP32 se reboot sau khi gui response.\n"; }
  if (cmd == "factory-reset confirm") {
    clearNamespace("mesflow_cfg"); clearNamespace("mesflow");
    if (LittleFS.begin(true)) LittleFS.format();
    forceSetupOnNextBoot(); delayedReboot = true;
    return "Factory reset da thuc hien. ESP32 se reboot vao Setup Portal.\n";
  }
  return "Lenh khong hop le hoac thieu CONFIRM. Go help de xem danh sach.\n";
}

static String formatUptime(uint32_t seconds) {
  uint32_t days = seconds / 86400UL; seconds %= 86400UL;
  uint32_t hours = seconds / 3600UL; seconds %= 3600UL;
  uint32_t minutes = seconds / 60UL; seconds %= 60UL;
  char buf[48];
  snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu",
           static_cast<unsigned long>(days), static_cast<unsigned long>(hours),
           static_cast<unsigned long>(minutes), static_cast<unsigned long>(seconds));
  return String(buf);
}

static void handleDeviceHealthApi() {
  DynamicJsonDocument doc(4096);
  const bool wifiOk = WiFi.status() == WL_CONNECTED;
  const uint32_t now = millis();
  const uint32_t heapTotal = ESP.getHeapSize();
  const uint32_t heapFree = ESP.getFreeHeap();
  const uint32_t psramTotal = ESP.getPsramSize();
  const uint32_t psramFree = ESP.getFreePsram();
  size_t fsTotal = 0, fsUsed = 0;
  if (fsReady) { fsTotal = LittleFS.totalBytes(); fsUsed = LittleFS.usedBytes(); }

  doc["ok"] = true;
  doc["updated_ms"] = now;
  JsonObject device = doc.createNestedObject("device");
  device["firmware"] = APP_VERSION;
  device["model"] = "ESP32-S3 N16R8";
  device["device_id"] = DEVICE_ID;
  device["device_name"] = DEVICE_NAME;
  device["station"] = STATION_CODE;
  device["uptime_seconds"] = now / 1000UL;
  device["uptime"] = formatUptime(now / 1000UL);
  device["reset_reason"] = "See boot log";

  JsonObject cpu = doc.createNestedObject("cpu");
  cpu["frequency_mhz"] = ESP.getCpuFreqMHz();
  cpu["temperature_c"] = temperatureRead();
  cpu["load_supported"] = false;
  cpu["note"] = "CPU load is not estimated to avoid misleading values";

  JsonObject memory = doc.createNestedObject("memory");
  memory["heap_total"] = heapTotal;
  memory["heap_free"] = heapFree;
  memory["heap_min_free"] = ESP.getMinFreeHeap();
  memory["heap_largest_block"] = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  memory["psram_total"] = psramTotal;
  memory["psram_free"] = psramFree;

  JsonObject storage = doc.createNestedObject("storage");
  storage["flash_size"] = ESP.getFlashChipSize();
  storage["littlefs_ready"] = fsReady;
  storage["littlefs_total"] = fsTotal;
  storage["littlefs_used"] = fsUsed;
  storage["littlefs_free"] = fsTotal >= fsUsed ? fsTotal - fsUsed : 0;

  JsonObject network = doc.createNestedObject("network");
  network["wifi_connected"] = wifiOk;
  network["ssid"] = wifiOk ? WiFi.SSID() : String(WIFI_SSID);
  network["rssi"] = wifiOk ? WiFi.RSSI() : -127;
  network["ip"] = wifiOk ? WiFi.localIP().toString() : "0.0.0.0";
  network["gateway"] = wifiOk ? WiFi.gatewayIP().toString() : "0.0.0.0";
  network["dns"] = wifiOk ? WiFi.dnsIP().toString() : "0.0.0.0";
  network["mac"] = WiFi.macAddress();
  network["server_url"] = SERVER_BASE;
  network["server_online"] = rt.online;
  network["last_http_status"] = rt.lastHttpStatus;

  JsonObject hardware = doc.createNestedObject("hardware");
  hardware["lcd"] = "ILI9341 READY";
  hardware["touch"] = touchAvailable ? "FT6336G READY" : "NOT FOUND";
  hardware["keypad"] = !keypadAvailable ? "NOT FOUND" :
      (keypadCalibrationInProgress ? "PCF8574T CALIBRATING" :
       (keypadMappingReady ? "PCF8574T READY" : "PCF8574T NEEDS CALIBRATION"));
  hardware["keypad_address"] = keypadAddress;
  hardware["keypad_calibrated"] = keypadMappingReady;
  hardware["scanner_uart_configured"] = true;
  hardware["scanner_status"] = "UART1: RX GPIO44, TX GPIO43, 9600 8N1";
  hardware["usb_serial_status"] = "READY";
  hardware["last_input_source"] = lastInputAtMs == 0 ? "NONE" :
      (lastInputWasVirtual ? "WEB VIRTUAL" : (lastInputWasKeypad ? "I2C KEYPAD" : "SERIAL/SCANNER"));
  hardware["last_input_age_ms"] = lastInputAtMs == 0 ? 0 : now - lastInputAtMs;
  hardware["input_event_count"] = inputEventCount;
  hardware["last_input_preview"] = lastInputPreview;

  JsonObject mes = doc.createNestedObject("mes");
  mes["bound"] = rt.bound;
  mes["online"] = rt.online;
  mes["ui_state"] = stateName(uiState);
  mes["workers"] = workerCacheCount;
  mes["operations"] = operationCacheCount;
  mes["offline_queue"] = countPendingOfflineEvents();
  mes["pending_transaction"] = hasPendingTransaction();
  mes["last_error"] = rt.lastError;

  int score = 100;
  if (!wifiOk) score -= 30; else if (WiFi.RSSI() < -80) score -= 15; else if (WiFi.RSSI() < -70) score -= 7;
  if (!rt.online) score -= 25;
  if (!touchAvailable) score -= 10;
  if (heapTotal && heapFree * 100UL / heapTotal < 15) score -= 15;
  if (countPendingOfflineEvents() > 0) score -= 5;
  if (score < 0) score = 0;
  doc["health_score"] = score;
  doc["health_note"] = "UART scanner on board P2: RX GPIO44, TX GPIO43, 9600 8N1";

  String body; serializeJson(doc, body);
  provisionWeb.send(200, "application/json", body);
  lastHealthRequestAtMs = now;
}

static String webConsolePage() {
  return String(F(R"HTML(<!doctype html><html lang="vi"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>MESFlow Device Manager</title><style>
:root{--bg:#f1f5f9;--card:#fff;--text:#0f172a;--muted:#64748b;--line:#e2e8f0;--primary:#2563eb;--ok:#16a34a;--warn:#d97706;--err:#dc2626}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:Arial,sans-serif}.wrap{max-width:1200px;margin:auto;padding:16px}.top{display:flex;justify-content:space-between;align-items:center;gap:12px;flex-wrap:wrap;border-top:4px solid var(--primary);padding-top:12px}.pill{padding:6px 12px;border-radius:99px;background:#fef3c7;color:#92400e;font-weight:700}.tabs{display:flex;gap:6px;flex-wrap:wrap;margin:14px 0}.tabs button{background:#fff;color:#334155;border:1px solid var(--line)}.tabs button.active{background:var(--primary);color:#fff}.panel{display:none}.panel.active{display:block}.cards{display:grid;grid-template-columns:repeat(4,1fr);gap:10px}.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:12px;box-shadow:0 2px 8px rgba(15,23,42,.04)}.metric{border-left:4px solid #94a3b8}.metric.ok{border-left-color:var(--ok)}.metric.warn{border-left-color:var(--warn)}.metric.err{border-left-color:var(--err)}.metric small,.muted{color:var(--muted)}.metric b{display:block;font-size:18px;margin-top:5px}.kv{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px 18px}.kv div{padding:8px 0;border-bottom:1px solid var(--line)}.kv span{display:block;color:var(--muted);font-size:12px}.grid2{display:grid;grid-template-columns:1fr 1fr;gap:12px}input,select,button{font:inherit;border-radius:8px;border:1px solid #cbd5e1;padding:10px;background:#fff;color:var(--text)}input,select{width:100%}button{cursor:pointer;background:var(--primary);color:#fff;border:0;font-weight:700}.secondary{background:#64748b}.danger{background:#dc2626}.row,.actions{display:flex;gap:8px;flex-wrap:wrap}.row input{flex:1}.terminal{white-space:pre-wrap;word-break:break-word;min-height:300px;max-height:560px;overflow:auto;background:#0f172a;color:#dbe4ee;padding:14px;border-radius:8px;font:14px/1.45 Consolas,monospace}.bar{height:9px;background:#e2e8f0;border-radius:99px;overflow:hidden;margin-top:7px}.bar i{display:block;height:100%;background:#2563eb;width:0}.health{font-size:34px;font-weight:800}.log-ok{color:#86efac}.log-warn{color:#fcd34d}.log-err{color:#fca5a5}.log-info{color:#7dd3fc}@media(max-width:800px){.cards{grid-template-columns:1fr 1fr}.grid2{grid-template-columns:1fr}}@media(max-width:460px){.cards{grid-template-columns:1fr}.kv{grid-template-columns:1fr}}</style></head><body><div class="wrap">
<div class="top"><div><h2 style="margin:0">MESFlow Web Device Manager</h2><div class="muted">Thông tin kỹ thuật chỉ hiển thị trên Web Console</div></div><span id="conn" class="pill">Đang kết nối...</span></div>
<div class="tabs"><button class="active" data-tab="dashboard">Dashboard</button><button data-tab="network">Network</button><button data-tab="hardware">Hardware</button><button data-tab="mes">MES</button><button data-tab="scanner">Máy quét ảo</button><button data-tab="logs">Logs & Diagnostics</button></div>
<section id="dashboard" class="panel active"><div class="cards"><div class="card metric" id="healthCard"><small>Device Health</small><b id="health">--</b></div><div class="card metric"><small>Firmware</small><b id="firmware">--</b></div><div class="card metric"><small>Uptime</small><b id="uptime">--</b></div><div class="card metric"><small>CPU</small><b id="cpu">--</b></div></div><div class="grid2"><div class="card"><b>Memory</b><div class="kv"><div><span>Heap free</span><strong id="heapFree">--</strong></div><div><span>Heap min</span><strong id="heapMin">--</strong></div><div><span>Largest block</span><strong id="heapLargest">--</strong></div><div><span>PSRAM free / total</span><strong id="psram">--</strong></div></div></div><div class="card"><b>Storage</b><div class="kv"><div><span>Flash</span><strong id="flash">--</strong></div><div><span>LittleFS</span><strong id="littlefs">--</strong></div><div><span>LittleFS used</span><strong id="fsUsed">--</strong></div><div><span>Model</span><strong id="model">--</strong></div></div></div></div></section>
<section id="network" class="panel"><div class="grid2"><div class="card"><b>Wi-Fi</b><div class="kv"><div><span>Status</span><strong id="wifiStatus">--</strong></div><div><span>SSID</span><strong id="ssid">--</strong></div><div><span>RSSI</span><strong id="rssi">--</strong></div><div><span>IP</span><strong id="ip">--</strong></div><div><span>Gateway</span><strong id="gateway">--</strong></div><div><span>DNS</span><strong id="dns">--</strong></div><div><span>MAC</span><strong id="mac">--</strong></div></div></div><div class="card"><b>MES Server</b><div class="kv"><div><span>URL</span><strong id="serverUrl">--</strong></div><div><span>Status</span><strong id="serverStatus">--</strong></div><div><span>Last HTTP</span><strong id="httpStatus">--</strong></div></div><div class="actions" style="margin-top:12px"><button onclick="quick('heartbeat')">Test server</button><button class="secondary" onclick="quick('bind')">Bind lại</button></div></div></div></section>
<section id="hardware" class="panel"><div class="grid2"><div class="card"><b>Thiết bị</b><div class="kv"><div><span>LCD</span><strong id="lcd">--</strong></div><div><span>Touch</span><strong id="touch">--</strong></div><div><span>I2C keypad</span><strong id="keypad">--</strong></div><div><span>UART scanner</span><strong id="uartScanner">--</strong></div><div><span>USB Serial</span><strong id="usbSerial">--</strong></div></div><div class="actions" style="margin-top:12px"><button class="danger" onclick="if(confirm('Hiệu chỉnh lại keypad ngay? Kiosk sẽ tạm khóa thao tác và không khởi động lại.'))quick('keypad-calibrate CONFIRM')">Hiệu chỉnh lại keypad</button></div></div><div class="card"><b>Input activity</b><div class="kv"><div><span>Nguồn gần nhất</span><strong id="inputSource">--</strong></div><div><span>Lần cuối</span><strong id="inputAge">--</strong></div><div><span>Tổng sự kiện</span><strong id="inputCount">--</strong></div><div><span>Dữ liệu gần nhất</span><strong id="inputPreview">--</strong></div></div><p class="muted">Keypad PCF8574T và cảm ứng dùng chung I2C GPIO16/GPIO15. Bảy dây keypad có thể cắm vào P0..P7 theo thứ tự bất kỳ sau khi hiệu chỉnh. Máy quét UART dùng RX GPIO44, 9600 8N1.</p></div></div></section>
<section id="mes" class="panel"><div class="cards"><div class="card metric"><small>Bind</small><b id="bound">--</b></div><div class="card metric"><small>Workers</small><b id="workers">--</b></div><div class="card metric"><small>Operations</small><b id="operations">--</b></div><div class="card metric"><small>Offline queue</small><b id="queue">--</b></div></div><div class="card"><div class="kv"><div><span>Device ID</span><strong id="deviceId">--</strong></div><div><span>Station</span><strong id="station">--</strong></div><div><span>UI state</span><strong id="uiState">--</strong></div><div><span>Pending transaction</span><strong id="pending">--</strong></div><div><span>Last error</span><strong id="lastError">--</strong></div></div><div class="actions" style="margin-top:12px"><button onclick="refreshMesCatalog()">Tải catalog từ MES</button><button class="secondary" onclick="window.open('/api/device/catalog-debug','_blank')">Xem catalog debug</button><button class="secondary" onclick="window.open('/api/device/catalog-raw','_blank')">Xem JSON gốc</button><button class="secondary" onclick="quick('retry')">Đồng bộ lại</button></div></div></section>
<section id="scanner" class="panel"><div class="card"><b>Máy quét ảo</b><p class="muted">Dùng dữ liệu thật từ MES để test luồng kiosk.</p><div class="grid2"><div><select id="workerQr"><option value="">-- Chọn QR nhân viên --</option></select><input id="workerCustom" style="margin-top:7px" placeholder="Hoặc nhập WF|EMP|..."><button style="margin-top:7px" onclick="sendVirtual('worker')">Quét nhân viên</button></div><div><select id="opQr"><option value="">-- Chọn QR operation --</option></select><input id="opCustom" style="margin-top:7px" placeholder="Hoặc nhập WF|OP|..."><button style="margin-top:7px" onclick="sendVirtual('op')">Quét operation</button></div></div><div class="row" style="margin-top:10px"><input id="qty" inputmode="numeric" value="1"><button onclick="sendQty()">Gửi số</button><button class="secondary" onclick="quick('scan 1')">Xác nhận</button><button class="secondary" onclick="quick('scan 2')">Sửa lại</button><button class="secondary" onclick="quick('reset')">READY</button></div></div></section>
<section id="logs" class="panel"><div class="card"><div class="row"><input id="cmd" value="status" onkeydown="if(event.key==='Enter')runCmd()"><button onclick="runCmd()">Gửi</button><button class="secondary" onclick="refreshLogs()">Làm mới log</button><button class="secondary" onclick="clearOut()">Xóa</button></div></div><div class="card"><div id="out" class="terminal">Đang kết nối tới ESP32...</div></div></section>
</div><script>
const $=id=>document.getElementById(id),out=$('out');let timer=null;function esc(t){return String(t??'').replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]))}function paint(t){return esc(t).split('\n').map(x=>{let c=/ERROR|FAIL|LOI|HTTP 5/i.test(x)?'log-err':/WARN|OFFLINE|RETRY/i.test(x)?'log-warn':/ OK|CONNECTED|ONLINE|READY/i.test(x)?'log-ok':/^>|INFO|BIND|HEARTBEAT/i.test(x)?'log-info':'';return '<span class="'+c+'">'+x+'</span>'}).join('\n')}function append(t){out.innerHTML+=paint(t);out.scrollTop=out.scrollHeight}function clearOut(){out.innerHTML=''}function setConn(ok){let e=$('conn');e.textContent=ok?'Đã kết nối':'Mất kết nối';e.style.background=ok?'#dcfce7':'#fee2e2';e.style.color=ok?'#166534':'#991b1b'}function fmtBytes(n){n=Number(n||0);if(n>=1048576)return (n/1048576).toFixed(2)+' MB';if(n>=1024)return (n/1024).toFixed(1)+' KB';return n+' B'}function age(ms){if(!ms)return 'Chưa có';let s=Math.floor(ms/1000);return s<60?s+' giây trước':Math.floor(s/60)+' phút trước'}function val(id,v){$(id).textContent=v??'--'}
document.querySelectorAll('.tabs button').forEach(b=>b.onclick=()=>{document.querySelectorAll('.tabs button').forEach(x=>x.classList.remove('active'));document.querySelectorAll('.panel').forEach(x=>x.classList.remove('active'));b.classList.add('active');$(b.dataset.tab).classList.add('active')});
async function health(){try{let r=await fetch('/api/device/health',{cache:'no-store'});let j=await r.json();if(!r.ok)throw Error('HTTP '+r.status);setConn(true);val('health',j.health_score+'/100');$('healthCard').className='card metric '+(j.health_score>=85?'ok':j.health_score>=60?'warn':'err');val('firmware',j.device.firmware);val('uptime',j.device.uptime);val('cpu',j.cpu.frequency_mhz+' MHz · '+Number(j.cpu.temperature_c).toFixed(1)+'°C');val('heapFree',fmtBytes(j.memory.heap_free)+' / '+fmtBytes(j.memory.heap_total));val('heapMin',fmtBytes(j.memory.heap_min_free));val('heapLargest',fmtBytes(j.memory.heap_largest_block));val('psram',fmtBytes(j.memory.psram_free)+' / '+fmtBytes(j.memory.psram_total));val('flash',fmtBytes(j.storage.flash_size));val('littlefs',j.storage.littlefs_ready?'READY':'NOT READY');val('fsUsed',fmtBytes(j.storage.littlefs_used)+' / '+fmtBytes(j.storage.littlefs_total));val('model',j.device.model);val('wifiStatus',j.network.wifi_connected?'CONNECTED':'DISCONNECTED');val('ssid',j.network.ssid);val('rssi',j.network.rssi+' dBm');val('ip',j.network.ip);val('gateway',j.network.gateway);val('dns',j.network.dns);val('mac',j.network.mac);val('serverUrl',j.network.server_url);val('serverStatus',j.network.server_online?'ONLINE':'OFFLINE');val('httpStatus',j.network.last_http_status);val('lcd',j.hardware.lcd);val('touch',j.hardware.touch);val('keypad',j.hardware.keypad+(j.hardware.keypad_address?' @ 0x'+Number(j.hardware.keypad_address).toString(16).toUpperCase():''));val('uartScanner',j.hardware.scanner_uart_configured?'CONFIGURED':j.hardware.scanner_status);val('usbSerial',j.hardware.usb_serial_status);val('inputSource',j.hardware.last_input_source);val('inputAge',age(j.hardware.last_input_age_ms));val('inputCount',j.hardware.input_event_count);val('inputPreview',j.hardware.last_input_preview||'--');val('bound',j.mes.bound?'YES':'NO');val('workers',j.mes.workers);val('operations',j.mes.operations);val('queue',j.mes.offline_queue);val('deviceId',j.device.device_id);val('station',j.device.station);val('uiState',j.mes.ui_state);val('pending',j.mes.pending_transaction?'YES':'NO');val('lastError',j.mes.last_error||'Không có')}catch(e){setConn(false)}}
async function api(command,silent=false){try{let r=await fetch('/api/device/console',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({command})});let j=await r.json();if(!r.ok)throw Error(j.message||('HTTP '+r.status));setConn(true);if(!silent)append('> '+command+'\n'+j.output+'\n');return j}catch(e){setConn(false);if(!silent)append('[ERROR] '+e+'\n')}}function runCmd(){let c=$('cmd').value.trim();if(c)api(c)}function quick(c){$('cmd').value=c;return api(c)}async function refreshLogs(){let a=await api('status',true),l=await api('logs',true);if(a&&l)out.innerHTML=paint(a.output+'\n--- RECENT LOGS ---\n'+l.output)}
async function loadTestData(){try{let r=await fetch('/api/device/test-data',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);let j=await r.json(),w=$('workerQr'),o=$('opQr');w.innerHTML='<option value="">-- Chọn QR nhân viên --</option>';o.innerHTML='<option value="">-- Chọn QR operation --</option>';(j.workers||[]).forEach(x=>w.add(new Option((x.code||x.qr)+' - '+(x.name||''),x.qr)));(j.operations||[]).forEach(x=>o.add(new Option((x.name||x.qr)+(x.po?' · '+x.po:''),x.qr)));if(!(j.workers||[]).length&&!(j.operations||[]).length)append('[WARN] Cache QR đang trống. Bấm Tải catalog từ MES.\n');else append('[OK] Đã nạp '+(j.workers||[]).length+' nhân viên, '+(j.operations||[]).length+' operation vào danh sách.\n')}catch(e){append('[ERROR] Không tải được QR: '+e+'\n')}}async function refreshMesCatalog(){try{append('[INFO] Đang tải catalog từ MES...\n');let r=await fetch('/api/device/catalog-refresh',{method:'POST'}),j=await r.json();if(!r.ok||!j.ok)throw Error(j.message||('HTTP '+r.status));append('[OK] '+j.workers+' nhân viên, '+j.operations+' operation · '+(j.message||'OK')+'\n');await loadTestData();await health()}catch(e){append('[ERROR] '+e+'\n')}}function sendVirtual(type){let v=$(type==='worker'?'workerCustom':'opCustom').value.trim()||$(type==='worker'?'workerQr':'opQr').value;if(!v)return append('[WARN] Chưa chọn QR.\n');quick('scan '+v).then(health)}function sendQty(){let v=$('qty').value.trim();if(!/^\d+$/.test(v))return append('[WARN] Số lượng không hợp lệ.\n');quick('scan '+v).then(health)}
health();loadTestData();refreshLogs();timer=setInterval(health,5000);
</script></body></html>)HTML"));
}

// Web commands that affect the kiosk are acknowledged immediately and then
// executed from loop(). This prevents the synchronous WebServer handler from
// waiting through a MES lookup/START/FINISH HTTP transaction.
static String pendingWebCommand;
static bool pendingWebCommandReady = false;
static uint32_t lastWebCommandAcceptedAt = 0;

static bool isDeferredWebCommand(const String& lower) {
  return lower.startsWith("scan ") || lower == "reset" || lower == "r" ||
         lower == "touch-test" || lower == "touch";
}

static void servicePendingWebCommand() {
  if (!pendingWebCommandReady) return;
  String command = pendingWebCommand;
  pendingWebCommand = "";
  pendingWebCommandReady = false;
  bool delayedReboot = false, openSetup = false;
  String output = executeRemoteCommand(command, delayedReboot, openSetup);
  remoteLogf("WEB async completed: %s", command.c_str());
  Serial.printf("[WEB ASYNC] %s\n%s", command.c_str(), output.c_str());
}

static void handleRemoteConsoleApi() {
  // Console LAN v3.6.0: khong can token. Lenh nguy hiem van bat buoc CONFIRM.
  DynamicJsonDocument req(512), resp(4096);
  DeserializationError err = deserializeJson(req, provisionWeb.arg("plain"));
  if (err) { provisionWeb.send(400, "application/json", "{\"ok\":false,\"message\":\"JSON khong hop le\"}"); return; }
  String command = req["command"] | "";
  String lower = command; lower.trim(); lower.toLowerCase();
  if (isDeferredWebCommand(lower)) {
    if (pendingWebCommandReady) {
      provisionWeb.send(409, "application/json", "{\"ok\":false,\"message\":\"ESP dang xu ly lenh truoc\"}");
      return;
    }
    pendingWebCommand = command;
    pendingWebCommandReady = true;
    lastWebCommandAcceptedAt = millis();
    DynamicJsonDocument queued(512);
    queued["ok"] = true; queued["queued"] = true; queued["command"] = command;
    queued["output"] = "Da nhan lenh; ESP se xu ly ngay trong loop ke tiep.\n";
    String queuedBody; serializeJson(queued, queuedBody);
    provisionWeb.send(202, "application/json", queuedBody);
    return;
  }
  bool delayedReboot = false, openSetup = false;
  String output = executeRemoteCommand(command, delayedReboot, openSetup);
  resp["ok"] = true; resp["command"] = command; resp["output"] = output;
  String body; serializeJson(resp, body);
  provisionWeb.send(200, "application/json", body);
  (void)openSetup;
  if (delayedReboot) { delay(500); ESP.restart(); }
}


struct CatalogDebugState {
  String stage = "never-run";
  String url;
  String contentType;
  String preview;
  String parseError;
  int httpStatus = 0;
  int declaredLength = -1;
  int written = -1;
  size_t actualLength = 0;
  uint16_t workersJson = 0;
  uint16_t operationsJson = 0;
  uint16_t workersLoaded = 0;
  uint16_t operationsLoaded = 0;
  uint32_t elapsedMs = 0;
  uint32_t freeHeapBefore = 0;
  uint32_t freeHeapAfter = 0;
  uint32_t freePsramBefore = 0;
  uint32_t freePsramAfter = 0;
};
static CatalogDebugState catalogDebug;
static const char* CATALOG_TEMP_PATH = "/catalog_debug.tmp";

static void catalogStage(const char* stage) {
  catalogDebug.stage = stage;
  remoteLogf("CATDBG stage=%s heap=%u psram=%u", stage,
             static_cast<unsigned>(ESP.getFreeHeap()),
             static_cast<unsigned>(ESP.getFreePsram()));
  Serial.printf("[CATDBG] stage=%s heap=%u psram=%u\n", stage,
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getFreePsram()));
}

static bool refreshCatalogFromMes(String& message, uint16_t& workersLoaded, uint16_t& operationsLoaded) {
  workersLoaded = 0;
  operationsLoaded = 0;
  catalogDebug = CatalogDebugState();
  catalogDebug.freeHeapBefore = ESP.getFreeHeap();
  catalogDebug.freePsramBefore = ESP.getFreePsram();
  catalogStage("precheck");
  if (WiFi.status() != WL_CONNECTED) { message = "WiFi chua ket noi"; catalogStage("fail-wifi"); return false; }
  if (!fsReady) { message = "LittleFS chua san sang"; catalogStage("fail-littlefs"); return false; }
  if (!offlineBuffersReady && !allocateOfflineBuffers()) {
    message = "Khong cap phat duoc cache HEAP/PSRAM";
    catalogStage("fail-cache-buffers");
    return false;
  }

  // Normalize SERVER_BASE so a saved URL ending in '/' does not create '//api/...'.
  String base = String(SERVER_BASE);
  base.trim();
  while (base.endsWith("/")) base.remove(base.length() - 1);
  String url = base + "/api/kiosk/offline-snapshot";
  catalogDebug.url = url;
  remoteLogf("CATDBG url=%s wifi=%s ip=%s rssi=%d fs_total=%u fs_used=%u", url.c_str(), WIFI_SSID, WiFi.localIP().toString().c_str(), WiFi.RSSI(), static_cast<unsigned>(LittleFS.totalBytes()), static_cast<unsigned>(LittleFS.usedBytes()));
  catalogStage("http-begin");

  MesHttpSession net;
  // Flask/nginx may return a chunked HTTP/1.1 body. HTTP/1.0 makes the
  // response easier and more reliable to persist before JSON parsing.
  if (!net.begin(url, 15000, 35000, 35, true, true)) {
    message = "http.begin that bai: " + url;
    remoteLogf("CATALOG BEGIN FAIL url=%s", url.c_str());
    return false;
  }
  HTTPClient& http = net.http();
  // /api/demo-codes is a kiosk/MES endpoint and may require the same
  // station/device/token identity as lookup, heartbeat and session APIs.
  addAuthHeaders(http);
  http.addHeader("Accept", "application/json");
  http.addHeader("Cache-Control", "no-cache");

  const uint32_t started = millis();
  int status = http.GET();
  const int declaredLength = http.getSize();
  const String contentType = http.header("Content-Type");
  catalogDebug.httpStatus = status;
  catalogDebug.declaredLength = declaredLength;
  catalogDebug.contentType = contentType;
  remoteLogf("CATDBG headers status=%d declared=%d type=%s location=%s", status, declaredLength, contentType.c_str(), http.header("Location").c_str());
  catalogStage("http-response");
  if (status < 200 || status >= 300) {
    message = status > 0 ? String("MES HTTP ") + status : HTTPClient::errorToString(status);
    remoteLogf("CATALOG HTTP FAIL status=%d len=%d type=%s url=%s", status,
               declaredLength, contentType.c_str(), url.c_str());
    net.end();
    return false;
  }

  const char* tempPath = CATALOG_TEMP_PATH;
  LittleFS.remove(tempPath);
  File temp = LittleFS.open(tempPath, "w");
  if (!temp) {
    message = "Khong tao duoc /catalog.tmp";
    net.end();
    return false;
  }

  // Download first, parse second. This avoids deserializeJson() reading a
  // temporarily empty/chunked WiFi stream and also leaves exact byte diagnostics.
  catalogStage("body-download");
  int written = http.writeToStream(&temp);
  temp.flush();
  size_t actualLength = temp.size();
  catalogDebug.written = written;
  catalogDebug.actualLength = actualLength;
  remoteLogf("CATDBG body written=%d actual=%u declared=%d", written, static_cast<unsigned>(actualLength), declaredLength);
  temp.close();
  net.end();

  if (written < 0 || actualLength == 0) {
    message = String("Tai body that bai: ") + written + ", bytes=" + actualLength;
    remoteLogf("CATALOG BODY FAIL written=%d actual=%u declared=%d", written,
               static_cast<unsigned>(actualLength), declaredLength);
    LittleFS.remove(tempPath);
    return false;
  }

  catalogStage("body-saved");
  File input = LittleFS.open(tempPath, "r");
  if (!input) {
    message = "Khong mo lai duoc catalog.tmp";
    LittleFS.remove(tempPath);
    return false;
  }

  DynamicJsonDocument filter(1024);
  filter["workers"][0]["qr"] = true;
  filter["workers"][0]["employee_no"] = true;
  filter["workers"][0]["name"] = true;
  filter["revision"] = true;
  filter["operations"][0]["qr"] = true;
  filter["operations"][0]["name"] = true;
  filter["operations"][0]["po"] = true;
  filter["operations"][0]["part"] = true;

  // The filtered document is much smaller than the raw response. Use a
  // large PSRAM document when available, otherwise a bounded HEAP document.
  const size_t jsonCapacity = (psramFound() && ESP.getFreePsram() > 128U * 1024U)
                                ? 384U * 1024U : 96U * 1024U;
  PsramJsonDocument doc(jsonCapacity);
  if (doc.capacity() == 0) {
    input.close();
    message = String("Khong cap phat JSON doc ") + String(static_cast<unsigned>(jsonCapacity)) +
              "; heap=" + ESP.getFreeHeap() + "; psram=" + ESP.getFreePsram();
    catalogStage("fail-json-alloc");
    return false;
  }
  remoteLogf("CATDBG json capacity=%u mode=%s heap=%u psram=%u",
             static_cast<unsigned>(jsonCapacity),
             psramFound() ? "PSRAM/FALLBACK" : "HEAP",
             static_cast<unsigned>(ESP.getFreeHeap()),
             static_cast<unsigned>(ESP.getFreePsram()));

  DeserializationError err = deserializeJson(
      doc, input, DeserializationOption::Filter(filter));
  input.close();
  File previewFile = LittleFS.open(tempPath, "r");
  char preview[161] = {0};
    if (previewFile) {
    size_t n = previewFile.readBytes(preview, sizeof(preview) - 1);
    preview[n] = 0;
    previewFile.close();
  }
  catalogDebug.preview = preview;
  if (err) {
    catalogDebug.parseError = err.c_str();
    message = String("JSON ") + err.c_str() + "; bytes=" + actualLength + "; dau=" + preview;
    remoteLogf("CATALOG PARSE FAIL actual=%u declared=%d err=%s preview=%.80s",
               static_cast<unsigned>(actualLength), declaredLength, err.c_str(), preview);
    catalogStage("fail-json-parse");
    return false;
  }
  catalogStage("json-parsed");

  JsonArray workers = doc["workers"].as<JsonArray>();
  JsonArray operations = doc["operations"].as<JsonArray>();
  safeCopy(offlineSnapshotRevision,sizeof(offlineSnapshotRevision),doc["revision"]|"unknown");
  prefs.begin("mesflow",false);prefs.putString("snap_rev",offlineSnapshotRevision);prefs.end();
  catalogDebug.workersJson = workers.size();
  catalogDebug.operationsJson = operations.size();
  remoteLogf("CATDBG arrays workers=%u operations=%u rootSize=%u", static_cast<unsigned>(workers.size()), static_cast<unsigned>(operations.size()), static_cast<unsigned>(doc.size()));
  if (workers.isNull() || operations.isNull()) {
    message = "JSON khong co workers/operations";
    remoteLogf("CATALOG INVALID ROOT bytes=%u", static_cast<unsigned>(actualLength));
    LittleFS.remove(tempPath);
    return false;
  }

  workerCacheCount = 0;
  operationCacheCount = 0;
  uint32_t epoch = currentEpoch();
  if (!epoch) epoch = 1;

  for (JsonObject x : workers) {
    const char* qr = x["qr"] | "";
    if (!qr[0] || workerCacheCount >= workerCacheCapacity) continue;
    CachedWorker& w = workerCache[workerCacheCount++];
    memset(&w, 0, sizeof(w));
    safeCopy(w.qr, sizeof(w.qr), qr);
    safeCopy(w.code, sizeof(w.code), x["employee_no"] | "");
    safeCopy(w.name, sizeof(w.name), x["name"] | "");
    w.cachedEpoch = epoch;
    sealObject(w);
  }

  for (JsonObject x : operations) {
    const char* qr = x["qr"] | "";
    if (!qr[0] || operationCacheCount >= operationCacheCapacity) continue;
    CachedOperation& o = operationCache[operationCacheCount++];
    memset(&o, 0, sizeof(o));
    safeCopy(o.qr, sizeof(o.qr), qr);
    safeCopy(o.name, sizeof(o.name), x["name"] | "");
    safeCopy(o.po, sizeof(o.po), x["po"] | "");
    safeCopy(o.part, sizeof(o.part), x["part"] | "");
    safeCopy(o.station, sizeof(o.station), STATION_CODE);
    o.cachedEpoch = epoch;
    sealObject(o);
  }

  catalogStage("cache-save");
  bool ws = saveArray(WORKER_CACHE_FILE, workerCache, workerCacheCount);
  bool os = saveArray(OP_CACHE_FILE, operationCache, operationCacheCount);
  workersLoaded = workerCacheCount;
  operationsLoaded = operationCacheCount;
  catalogDebug.workersLoaded = workersLoaded;
  catalogDebug.operationsLoaded = operationsLoaded;
  catalogDebug.elapsedMs = millis() - started;
  catalogDebug.freeHeapAfter = ESP.getFreeHeap();
  catalogDebug.freePsramAfter = ESP.getFreePsram();
  // Keep raw response in debug build so it can be downloaded from Web Console.
  remoteLogf("CATDBG cache save workers=%u operations=%u ws=%d os=%d workerFile=%u opFile=%u", workersLoaded, operationsLoaded, ws, os, static_cast<unsigned>(LittleFS.exists(WORKER_CACHE_FILE) ? LittleFS.open(WORKER_CACHE_FILE, "r").size() : 0), static_cast<unsigned>(LittleFS.exists(OP_CACHE_FILE) ? LittleFS.open(OP_CACHE_FILE, "r").size() : 0));

  if (!workersLoaded && !operationsLoaded) {
    message = "API tra JSON hop le nhung khong co QR";
    remoteLogf("CATALOG EMPTY workers_json=%u operations_json=%u",
               static_cast<unsigned>(workers.size()), static_cast<unsigned>(operations.size()));
    return false;
  }
  if (!ws || !os) {
    message = "Tai duoc nhung luu cache that bai";
    return false;
  }

  message = String("OK; HTTP ") + status + "; " + actualLength + " bytes; " +
            (millis() - started) + " ms; " + url;
  remoteLogf("CATALOG OK workers=%u operations=%u bytes=%u ms=%lu url=%s",
             workersLoaded, operationsLoaded, static_cast<unsigned>(actualLength),
             static_cast<unsigned long>(millis() - started), url.c_str());
  Serial.printf("[CATALOG] workers=%u operations=%u bytes=%u ms=%lu url=%s\n",
                workersLoaded, operationsLoaded, static_cast<unsigned>(actualLength),
                static_cast<unsigned long>(millis() - started), url.c_str());
  catalogStage("success");
  return true;
}

static void handleCatalogDebugApi() {
  DynamicJsonDocument doc(4096);
  doc["version"] = APP_VERSION;
  doc["stage"] = catalogDebug.stage;
  doc["url"] = catalogDebug.url;
  doc["http_status"] = catalogDebug.httpStatus;
  doc["content_type"] = catalogDebug.contentType;
  doc["declared_length"] = catalogDebug.declaredLength;
  doc["written"] = catalogDebug.written;
  doc["actual_length"] = catalogDebug.actualLength;
  doc["preview"] = catalogDebug.preview;
  doc["parse_error"] = catalogDebug.parseError;
  doc["workers_json"] = catalogDebug.workersJson;
  doc["operations_json"] = catalogDebug.operationsJson;
  doc["workers_loaded"] = catalogDebug.workersLoaded;
  doc["operations_loaded"] = catalogDebug.operationsLoaded;
  doc["elapsed_ms"] = catalogDebug.elapsedMs;
  doc["free_heap_before"] = catalogDebug.freeHeapBefore;
  doc["free_heap_after"] = catalogDebug.freeHeapAfter;
  doc["free_psram_before"] = catalogDebug.freePsramBefore;
  doc["free_psram_after"] = catalogDebug.freePsramAfter;
  doc["temp_exists"] = LittleFS.exists(CATALOG_TEMP_PATH);
  doc["temp_size"] = LittleFS.exists(CATALOG_TEMP_PATH) ? LittleFS.open(CATALOG_TEMP_PATH, "r").size() : 0;
  doc["logs"] = remoteLogsText();
  String body; serializeJsonPretty(doc, body);
  provisionWeb.send(200, "application/json; charset=utf-8", body);
}

static void handleCatalogRawApi() {
  if (!LittleFS.exists(CATALOG_TEMP_PATH)) { provisionWeb.send(404, "text/plain", "No catalog raw file. Run catalog refresh first."); return; }
  File f = LittleFS.open(CATALOG_TEMP_PATH, "r");
  if (!f) { provisionWeb.send(500, "text/plain", "Cannot open catalog raw file"); return; }
  provisionWeb.streamFile(f, "application/json");
  f.close();
}

static void handleCatalogRefreshApi() {
  String message; uint16_t workersLoaded=0, operationsLoaded=0; bool ok=refreshCatalogFromMes(message,workersLoaded,operationsLoaded);
  DynamicJsonDocument doc(512); doc["ok"]=ok; doc["message"]=message; doc["workers"]=workersLoaded; doc["operations"]=operationsLoaded; doc["source"]=String(SERVER_BASE)+"/api/demo-codes";
  String body; serializeJson(doc,body); provisionWeb.send(ok?200:502,"application/json",body);
}

static void handleTestDataApi() {
  DynamicJsonDocument doc(24576);
  doc["ok"] = true;
  JsonArray workers = doc.createNestedArray("workers");
  for (uint16_t i = 0; i < workerCacheCount && i < 40; ++i) {
    JsonObject x = workers.createNestedObject();
    x["qr"] = workerCache[i].qr; x["code"] = workerCache[i].code; x["name"] = workerCache[i].name;
  }
  JsonArray operations = doc.createNestedArray("operations");
  for (uint16_t i = 0; i < operationCacheCount && i < 80; ++i) {
    JsonObject x = operations.createNestedObject();
    x["qr"] = operationCache[i].qr; x["name"] = operationCache[i].name; x["po"] = operationCache[i].po; x["part"] = operationCache[i].part;
  }
  doc["ui_state"] = stateName(uiState);
  String body; serializeJson(doc, body);
  provisionWeb.send(200, "application/json", body);
}

void startLanProvisioning() {
  if (lanProvisionActive || WiFi.status() != WL_CONNECTED) return;
  refreshProvisionToken();
  const char* headers[] = {"X-Provision-Token"};
  provisionWeb.collectHeaders(headers, 1);
  provisionWeb.on("/", HTTP_GET, [](){ provisionWeb.send(200, "text/html; charset=utf-8", webConsolePage()); });
  provisionWeb.on("/api/device/info", HTTP_GET, [](){ sendDeviceInfo(provisionWeb); });
  provisionWeb.on("/api/device/health", HTTP_GET, handleDeviceHealthApi);
  provisionWeb.on("/console", HTTP_GET, [](){ provisionWeb.send(200, "text/html; charset=utf-8", webConsolePage()); });
  provisionWeb.on("/api/device/console", HTTP_POST, handleRemoteConsoleApi);
  provisionWeb.on("/api/device/test-data", HTTP_GET, handleTestDataApi);
  provisionWeb.on("/api/device/catalog-refresh", HTTP_POST, handleCatalogRefreshApi);
  provisionWeb.on("/api/device/catalog-debug", HTTP_GET, handleCatalogDebugApi);
  provisionWeb.on("/api/device/catalog-raw", HTTP_GET, handleCatalogRawApi);
#if MESFLOW_UI_SCREENSHOT
  provisionWeb.on("/debug/screenshot", HTTP_GET, handleDebugScreenshot);
  provisionWeb.on("/debug/ui-state", HTTP_GET, handleDebugUiState);
  provisionWeb.on("/debug/screens", HTTP_GET, handleDebugScreens);
  provisionWeb.on("/debug/show-screen", HTTP_GET, handleDebugShowScreen);
#endif
  provisionWeb.on("/api/device/provision", HTTP_POST, handleLanProvision);
  provisionWeb.on("/api/device/reboot", HTTP_POST, [](){
    if (!provisionAuthorized()) { provisionWeb.send(403, "application/json", "{\"ok\":false}"); return; }
    provisionWeb.send(200, "application/json", "{\"ok\":true}"); delay(300); ESP.restart();
  });
  provisionWeb.begin();
  discoveryUdp.begin(DISCOVERY_PORT);
  String host = buildMdnsHostname();
  safeCopy(mdnsHostname, sizeof(mdnsHostname), host.c_str());
  if (MDNS.begin(host.c_str())) {
    mdnsReady = true;
    MDNS.addService("mesflow-kiosk", "tcp", PROVISION_HTTP_PORT);
    MDNS.addServiceTxt(String("mesflow-kiosk"), String("tcp"), String("device_id"), String(DEVICE_ID));
    MDNS.addServiceTxt(String("mesflow-kiosk"), String("tcp"), String("firmware"), String(APP_VERSION));
  } else mdnsReady = false;
  lanProvisionActive = true;
  Serial.printf("[WEB] Console http://%s:%u/  mDNS=http://%s.local:%u/\n", WiFi.localIP().toString().c_str(), PROVISION_HTTP_PORT, host.c_str(), PROVISION_HTTP_PORT);
  remoteLogf("WEB CONSOLE http://%s:%u/", WiFi.localIP().toString().c_str(), PROVISION_HTTP_PORT);
}

static void broadcastDiscovery() {
  if (!lanProvisionActive || WiFi.status() != WL_CONNECTED) return;
  DynamicJsonDocument doc(1024);
  doc["type"] = "mesflow_kiosk_discovery";
  doc["device_id"] = DEVICE_ID;
  doc["device_uuid"] = DEVICE_UUID;
  doc["device_name"] = DEVICE_NAME;
  doc["station_code"] = STATION_CODE;
  doc["firmware"] = APP_VERSION;
  doc["mac"] = WiFi.macAddress();
  doc["ip"] = WiFi.localIP().toString();
  doc["http_port"] = PROVISION_HTTP_PORT;
  doc["configured"] = rt.bound;
  doc["rssi"] = WiFi.RSSI();
  doc["provision_token"] = provisionToken;
  char body[1024]; size_t len = serializeJson(doc, body, sizeof(body));
  IPAddress broadcast = WiFi.localIP();
  IPAddress mask = WiFi.subnetMask();
  for (uint8_t i = 0; i < 4; ++i) broadcast[i] = WiFi.localIP()[i] | static_cast<uint8_t>(~mask[i]);
  discoveryUdp.beginPacket(broadcast, DISCOVERY_PORT);
  discoveryUdp.write(reinterpret_cast<const uint8_t*>(body), len);
  discoveryUdp.endPacket();
}

void serviceDeviceManagement() {
  if (setupPortalActive) {
    captiveDns.processNextRequest();
    setupWeb.handleClient();
    delay(2);
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    if (!lanProvisionActive) startLanProvisioning();
    provisionWeb.handleClient();
    if (millis() - lastDiscoveryAt >= DISCOVERY_INTERVAL_MS) {
      lastDiscoveryAt = millis();
      broadcastDiscovery();
    }
  }
}

String waitLabel(const char* base, uint8_t frame) {
  String label = base ? String(base) : String("");
  uint8_t dots = frame % 4;
  for (uint8_t i = 0; i < dots; ++i) label += ".";
  return label;
}

void drawAnimatedHeader(const char* base, uint8_t frame, uint16_t color = C_TEXT) {
  // Chi ve lai vung tieu de de tao hieu ung, tranh chop toan man hinh.
  tft.fillRect(0, 37, SW, 63, C_BG);
  String label = waitLabel(base, frame);
  printCenteredFit(label.c_str(), 57, 3, color, true);
}

bool touchReadRegister(uint8_t reg, uint8_t* data, size_t len) {
  touchWire.beginTransmission(TOUCH_ADDR);
  touchWire.write(reg);
  if (touchWire.endTransmission(false) != 0) return false;
  size_t got = touchWire.requestFrom((int)TOUCH_ADDR, (int)len);
  if (got != len) { while (touchWire.available()) touchWire.read(); return false; }
  for (size_t i = 0; i < len; ++i) data[i] = touchWire.read();
  return true;
}

bool initTouch() {
  pinMode(TOUCH_RST, OUTPUT);
  pinMode(TOUCH_INT, INPUT_PULLUP);

  // FT6336G reset: hold low, release, then allow controller to boot.
  digitalWrite(TOUCH_RST, LOW);
  delay(20);
  digitalWrite(TOUCH_RST, HIGH);
  delay(180);

  touchWire.begin(TOUCH_SDA, TOUCH_SCL, 100000);
  delay(10);

  Serial.printf("[I2C] Touch bus SDA=%d SCL=%d\n", TOUCH_SDA, TOUCH_SCL);
  bool foundAny = false;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    touchWire.beginTransmission(addr);
    if (touchWire.endTransmission() == 0) {
      foundAny = true;
      Serial.printf("[I2C] Found device: 0x%02X%s\n", addr,
                    addr == TOUCH_ADDR ? " (FT6336G expected)" : "");
    }
    delay(1);
  }
  if (!foundAny) Serial.println("[I2C] No devices found on touch bus");

  touchWire.beginTransmission(TOUCH_ADDR);
  touchAvailable = (touchWire.endTransmission() == 0);

  if (touchAvailable) {
    uint8_t chipId = 0, fwId = 0, vendorId = 0;
    bool chipOk = touchReadRegister(0xA3, &chipId, 1);
    bool fwOk = touchReadRegister(0xA6, &fwId, 1);
    bool vendorOk = touchReadRegister(0xA8, &vendorId, 1);
    Serial.printf("[TOUCH] FT6336G READY addr=0x%02X INT=%d", TOUCH_ADDR, digitalRead(TOUCH_INT));
    if (chipOk) Serial.printf(" chip=0x%02X", chipId);
    if (fwOk) Serial.printf(" fw=0x%02X", fwId);
    if (vendorOk) Serial.printf(" vendor=0x%02X", vendorId);
    Serial.println();
    remoteLogf("TOUCH FT6336G READY addr=0x%02X chip=0x%02X fw=0x%02X", TOUCH_ADDR, chipId, fwId);
  } else {
    Serial.printf("[TOUCH] FT6336G NOT FOUND at 0x%02X; check SDA/SCL/RST/power\n", TOUCH_ADDR);
    remoteLogf("TOUCH FT6336G NOT FOUND addr=0x%02X", TOUCH_ADDR);
  }
  return touchAvailable;
}

bool readTouchPoint(uint16_t& x, uint16_t& y) {
  if (!touchAvailable) return false;
  uint8_t b[5] = {0};
  // FT6336G: 0x02=touch count, 0x03..0x06=point-1 X/Y high+low.
  if (!touchReadRegister(0x02, b, sizeof(b))) return false;
  if ((b[0] & 0x0F) == 0) return false;
  uint16_t rawX = ((uint16_t)(b[1] & 0x0F) << 8) | b[2];
  uint16_t rawY = ((uint16_t)(b[3] & 0x0F) << 8) | b[4];
  // LCD rotation 0: FT6336G raw coordinates are expected in 240x320 portrait.
  x = constrain(rawX, 0, SW - 1);
  y = constrain(rawY, 0, SH - 1);
  return true;
}

bool pointIn(int16_t x, int16_t y, int16_t bx, int16_t by, int16_t bw, int16_t bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

void drawTouchButton(int16_t x, int16_t y, int16_t w, int16_t h,
                     const char* label, uint16_t accent, uint16_t count = 0) {
  tft.fillRoundRect(x, y, w, h, 6, C_PANEL);
  tft.drawRoundRect(x, y, w, h, 6, accent);
  printCentered(label, x + w / 2, y + 8, 1, C_TEXT, true);
  if (count) {
    char n[12]; snprintf(n, sizeof(n), "%u", count);
    printCentered(n, x + w / 2, y + 24, 1, accent, true);
  }
}

void drawTouchTestScreen() {
  tft.fillScreen(C_BG);
  printCenteredFit("KIỂM TRA CẢM ỨNG", 12, 2, C_INFO, true);
  char stat[42]; snprintf(stat, sizeof(stat), "LAN CHAM: %lu  X:%u Y:%u", (unsigned long)touchCount, touchX, touchY);
  printCenteredFit(stat, 36, 1, C_MUTED, false);

  drawTouchButton(10, 58, 102, 48, "NUT 1", C_INFO, touchButtonCount[0]);
  drawTouchButton(128, 58, 102, 48, "NUT 2", C_OK, touchButtonCount[1]);
  drawTouchButton(10, 116, 102, 48, "NUT 3", C_WARN, touchButtonCount[2]);
  drawTouchButton(128, 116, 102, 48, "NUT 4", C_ERR, touchButtonCount[3]);

  printText(12, 181, "VUỐT THANH:", 1, C_MUTED, true);
  tft.drawRoundRect(12, 198, 216, 22, 5, C_PANEL_2);
  int16_t fillW = map(touchSliderValue, 0, 100, 0, 212);
  if (fillW > 0) tft.fillRoundRect(14, 200, fillW, 18, 4, C_INFO);
  char pct[12]; snprintf(pct, sizeof(pct), "%d%%", touchSliderValue);
  printCentered(pct, SW / 2, 204, 1, C_TEXT, true);

  tft.drawRect(12, 231, 150, 66, C_PANEL_2);
  printText(17, 236, "VẼ/KÉO TAY Ở ĐÂY", 1, C_MUTED, false);
  drawTouchButton(174, 231, 56, 66, "THOAT", C_WARN);
  printCenteredFit("Chạm nút, vuốt và vẽ để kiểm tra", 304, 1, C_MUTED, false);
}

void refreshTouchTestHeader() {
  tft.fillRect(0, 28, SW, 25, C_BG);
  char stat[42]; snprintf(stat, sizeof(stat), "LAN CHAM: %lu  X:%u Y:%u", (unsigned long)touchCount, touchX, touchY);
  printCenteredFit(stat, 36, 1, C_MUTED, false);
}

void handleTouch() {
  uint16_t x = 0, y = 0;
  bool down = readTouchPoint(x, y);
  if (!down) { touchWasDown = false; return; }
  touchX = x; touchY = y;

  if (uiState == UiState::TOUCH_TEST) {
    // Continuous zones: slider and drawing pad.
    if (pointIn(x, y, 12, 194, 216, 32)) {
      touchSliderValue = constrain(map((int)x, 14, 226, 0, 100), 0, 100);
      tft.fillRect(13, 199, 214, 20, C_BG);
      tft.drawRoundRect(12, 198, 216, 22, 5, C_PANEL_2);
      int16_t fillW = map(touchSliderValue, 0, 100, 0, 212);
      if (fillW > 0) tft.fillRoundRect(14, 200, fillW, 18, 4, C_INFO);
      char pct[12]; snprintf(pct, sizeof(pct), "%d%%", touchSliderValue);
      printCentered(pct, SW / 2, 204, 1, C_TEXT, true);
    } else if (pointIn(x, y, 13, 232, 148, 64)) {
      tft.fillCircle(x, y, 2, C_INFO);
    }
  }

  lastUserActionAt = millis();
  const bool cancellable = uiState != UiState::READY && uiState != UiState::TOUCH_TEST;
  const bool inCancelZone = pointIn(x, y, 0, 270, 90, 50);
  if (cancellable && inCancelZone) {
    if (!cancelTouchActive) {
      cancelTouchActive = true;
      cancelTouchStartedAt = millis();
    } else if (millis() - cancelTouchStartedAt >= UI_CANCEL_HOLD_MS) {
      cancelTouchActive = false;
      recoverUiFromStuck("USER_HOLD_CANCEL", true);
      return;
    }
  } else {
    cancelTouchActive = false;
  }

  if (touchWasDown) return;
  touchWasDown = true;
  ++touchCount;
  remoteLogf("TOUCH x=%u y=%u state=%s", x, y, stateName(uiState));

  // The error footer is a normal tap target. A short tap must be enough to
  // return to the employee-card scan screen; long-hold remains supported.
  if (uiState == UiState::ERROR_STATE && inCancelZone) {
    recoverUiFromStuck("USER_TAPPED_ERROR_BACK", true);
    return;
  }

  if (uiState != UiState::TOUCH_TEST) return;

  if (pointIn(x, y, 10, 58, 102, 48)) { ++touchButtonCount[0]; drawTouchButton(10,58,102,48,"NUT 1",C_INFO,touchButtonCount[0]); }
  else if (pointIn(x, y, 128, 58, 102, 48)) { ++touchButtonCount[1]; drawTouchButton(128,58,102,48,"NUT 2",C_OK,touchButtonCount[1]); }
  else if (pointIn(x, y, 10, 116, 102, 48)) { ++touchButtonCount[2]; drawTouchButton(10,116,102,48,"NUT 3",C_WARN,touchButtonCount[2]); }
  else if (pointIn(x, y, 128, 116, 102, 48)) { ++touchButtonCount[3]; drawTouchButton(128,116,102,48,"NUT 4",C_ERR,touchButtonCount[3]); }
  else if (pointIn(x, y, 174, 231, 56, 66)) { setUi(UiState::READY); return; }
  refreshTouchTestHeader();
}

void drawIndustrialHeader(uint16_t statusColor = C_OK) {
  // One consistent header on every production screen.
  tft.fillRect(0, 0, SW, HEADER_H, C_BG);
  printText(10, 10, "Kimex", FONT_HEADER, C_TEXT, true);
  // Worker screens expose only signal strength; detailed network state remains
  // in hidden Maintenance and logs.
  tft.drawFastHLine(10, HEADER_H - 1, SW - 20, C_PANEL_2);
  serviceNetworkIndicator(true);
}

void drawPanel(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t border = C_PANEL_2) {
  tft.fillRoundRect(x, y, w, h, 10, C_PANEL);
  tft.drawRoundRect(x, y, w, h, 10, border);
}

void drawActionButton(const char* label, int16_t y, uint16_t color = C_HEADER) {
  tft.fillRoundRect(18, y, SW - 36, 44, 8, color);
  tft.drawRoundRect(18, y, SW - 36, 44, 8, C_INFO);
  printCenteredFit(label, y + 14, 2, C_TEXT, true);
}

void drawIdQrIcon(int16_t cx, int16_t cy, uint16_t color = C_HEADER) {
  // Single-weight blue line icon: employee badge + QR.
  tft.drawRoundRect(cx - 45, cy - 42, 58, 78, 7, color);
  tft.drawCircle(cx - 16, cy - 19, 10, color);
  tft.drawRoundRect(cx - 30, cy - 3, 28, 27, 6, color);
  tft.drawFastHLine(cx - 8, cy + 8, 13, color);
  tft.drawFastHLine(cx - 8, cy + 16, 13, color);
  tft.drawRect(cx + 20, cy - 20, 48, 48, color);
  tft.drawRect(cx + 25, cy - 15, 12, 12, color);
  tft.drawRect(cx + 47, cy - 15, 12, 12, color);
  tft.drawRect(cx + 25, cy + 7, 12, 12, color);
  tft.fillRect(cx + 45, cy + 5, 5, 5, color);
  tft.fillRect(cx + 54, cy + 13, 5, 5, color);
  tft.fillRect(cx + 61, cy + 4, 4, 14, color);
}

void drawRobotIcon(int16_t cx, int16_t cy, uint16_t color = C_HEADER) {
  // Abstract production-operation icon in the same line style.
  tft.drawCircle(cx + 31, cy - 30, 8, color);
  tft.drawLine(cx + 25, cy - 24, cx + 6, cy - 5, color);
  tft.drawCircle(cx, cy, 8, color);
  tft.drawLine(cx - 6, cy + 6, cx - 26, cy + 27, color);
  tft.drawCircle(cx - 31, cy + 32, 8, color);
  tft.drawFastHLine(cx - 68, cy + 52, 122, color);
  tft.drawRoundRect(cx - 63, cy + 28, 27, 22, 4, color);
  tft.drawRoundRect(cx + 18, cy + 30, 31, 20, 4, color);
  tft.fillCircle(cx - 52, cy + 56, 4, color);
  tft.fillCircle(cx + 29, cy + 56, 4, color);
}

void drawCheckIcon(int16_t cx, int16_t cy) {
  tft.drawCircle(cx, cy, 38, C_HEADER);
  tft.drawCircle(cx, cy, 35, C_INFO);
  tft.drawLine(cx - 18, cy, cx - 5, cy + 14, C_HEADER);
  tft.drawLine(cx - 5, cy + 14, cx + 23, cy - 17, C_HEADER);
  tft.drawLine(cx - 18, cy + 1, cx - 5, cy + 15, C_INFO);
  tft.drawLine(cx - 5, cy + 15, cx + 23, cy - 16, C_INFO);
}

void drawStatusGlyph(int16_t cx, int16_t cy, uint16_t color, char glyph) {
  // Color parameter is retained for compatibility, but the icon stays blue.
  (void)color;
  tft.drawCircle(cx, cy, 33, C_HEADER);
  tft.drawCircle(cx, cy, 30, C_INFO);
  char text[2] = {glyph, '\0'};
  printCentered(text, cx, cy - 13, 4, C_HEADER, true);
}

void drawReady(uint8_t frame = 0) {
  (void)frame;
  tft.fillScreen(C_BG);
  drawIndustrialHeader(rt.online ? C_OK : C_ERR);
  drawPageTitle(rt.online ? "QUÉT THẺ NHÂN VIÊN" : "MẤT KẾT NỐI", rt.online ? C_TEXT : C_ERR);
  drawCenteredTextFit("Đưa mã vào máy quét", SCREEN_LEFT_MARGIN, 166, 216, 36, FONT_SECTION, FONT_SECTION, 1, C_MUTED);
  drawFooter("", "");
}

void drawLoading(const char* title, const char* line) {
  tft.fillScreen(C_BG);
  drawIndustrialHeader(C_INFO);
  drawPageTitle(title, C_TEXT);
  if (line && line[0]) drawCenteredTextFit(line, SCREEN_LEFT_MARGIN, 150, 216, 36, FONT_SECTION, FONT_SECTION, 1, C_MUTED, true);
}

void drawWorker(uint8_t frame = 0) {
  (void)frame;
  tft.fillScreen(C_BG);
  drawIndustrialHeader(C_OK);
  drawSectionLabel("NHÂN VIÊN");
  drawCenteredTextFit(rt.workerName[0] ? rt.workerName : rt.workerCode, SCREEN_LEFT_MARGIN, 82, 216, 58, FONT_VALUE, FONT_VALUE - 1, 2, C_TEXT, true);
  drawCenteredTextFit("QUÉT CÔNG ĐOẠN", SCREEN_LEFT_MARGIN, 166, 216, 58, FONT_TITLE, FONT_TITLE - 1, 2, C_INFO, true);
  drawFooter("* HỦY", "");
}

void drawOperation() {
  tft.fillScreen(C_BG);
  drawIndustrialHeader(C_OK);
  drawSectionLabel("CÔNG ĐOẠN");
  drawCenteredTextFit(rt.operationName[0] ? rt.operationName : "CÔNG ĐOẠN", SCREEN_LEFT_MARGIN, 78, 216, 78, FONT_VALUE, FONT_VALUE - 1, 2, C_TEXT, true);
  char context[82]; snprintf(context, sizeof(context), "%s  %s", rt.po, rt.part);
  drawCenteredTextFit(context, SCREEN_LEFT_MARGIN, 184, 216, 34, FONT_SECTION, FONT_SECTION - 1, 1, C_MUTED);
  drawFooter("* QUAY LẠI", "# BẮT ĐẦU");
}

void drawStartSuccess() {
  tft.fillScreen(C_BG);
  drawIndustrialHeader(C_OK);
  drawSectionLabel("ĐANG LÀM");
  drawCenteredTextFit(rt.workerName, SCREEN_LEFT_MARGIN, 70, 216, 50, FONT_VALUE, FONT_VALUE - 1, 2, C_TEXT, true);
  drawCenteredTextFit(rt.operationName, SCREEN_LEFT_MARGIN, 132, 216, 58, FONT_VALUE, FONT_VALUE - 1, 2, C_TEXT, true);
  char context[82]; snprintf(context, sizeof(context), "%s  %s", rt.po, rt.part);
  drawCenteredTextFit(context, SCREEN_LEFT_MARGIN, 208, 216, 30, FONT_SECTION, FONT_SECTION - 1, 1, C_MUTED);
  drawFooter("* HỦY", "# KẾT THÚC");
}

void drawQtyInput(const char* title, int value) {
  tft.fillScreen(C_BG);
  drawIndustrialHeader(C_INFO);
  drawSectionLabel("KẾT THÚC");
  drawPageTitle(title);
  char number[18]; snprintf(number, sizeof(number), "%d", value);
  drawCenteredTextFit(number, SCREEN_LEFT_MARGIN, 135, 216, 96, FONT_QUANTITY, FONT_QUANTITY - 1, 1, C_TEXT, true);
  drawFooter("* XÓA", "# TIẾP");
}

void drawAskRework() {
  tft.fillScreen(C_BG);
  drawIndustrialHeader(C_INFO);
  drawKeyValueRow("LỖI TỔNG", demoDefectQty, 46);
  drawCenteredTextFit("CÓ LỖI SỬA ĐƯỢC?", SCREEN_LEFT_MARGIN, 86, 216, 58,
                      FONT_TITLE, FONT_TITLE - 1, 2, C_TEXT, true);
  drawOptionRow("1", "KHÔNG, XONG", 164);
  drawOptionRow("2", "CÓ, NHẬP SỐ", 208);
  drawFooter("* QUAY LẠI", "");
}

void drawConfirmQty() {
  tft.fillScreen(C_BG);
  drawIndustrialHeader(C_INFO);
  drawPageTitle("XÁC NHẬN");
  drawKeyValueRow("Đạt", demoGoodQty, 118);
  drawKeyValueRow("Lỗi tổng", demoDefectQty, 158);
  drawFooterTwoActions("* QUAY LẠI", "# XÁC NHẬN");
}

void drawFinishSuccess() {
  tft.fillScreen(C_BG);
  drawIndustrialHeader(C_OK);
  drawPageTitle("ĐÃ LƯU", C_OK);
  drawKeyValueRow("Đạt", demoGoodQty, 140);
  drawKeyValueRow("Lỗi tổng", demoDefectQty, 178);
}

// Worker-facing summary follows backend semantics directly: defect is total
// defective output, rework is its repairable subset, and scrap is derived.
void drawWorkerQtyConfirmation() {
  tft.fillScreen(C_BG);
  drawIndustrialHeader(C_INFO);
  drawPageTitle("XÁC NHẬN");
  drawKeyValueRow("Đạt", demoGoodQty, 112);
  if (demoReworkQty > 0) {
    drawKeyValueRow("Lỗi tổng", demoDefectQty, 146);
    drawKeyValueRow("Sửa được", demoReworkQty, 180);
    drawKeyValueRow("Phế", demoDefectQty - demoReworkQty, 214);
  } else {
    drawKeyValueRow("Lỗi tổng", demoDefectQty, 154);
  }
  drawFooter("* QUAY LẠI", "# XÁC NHẬN");
}

void drawFinishRetry() {
  tft.fillScreen(C_BG);
  drawIndustrialHeader(C_ERR);
  drawPageTitle("CHƯA GỬI ĐƯỢC", C_ERR);
  drawCenteredTextFit(rt.lastError[0] ? rt.lastError : "MẤT KẾT NỐI MÁY CHỦ",
                      SCREEN_LEFT_MARGIN, 128, 216, 82,
                      FONT_SECTION, FONT_SECTION - 1, 3, C_TEXT, true);
  drawFooterTwoActions("* QUAY LẠI", "# THỬ LẠI");
}

void drawWorkerFinishSuccess() {
  tft.fillScreen(C_BG);
  drawIndustrialHeader(C_OK);
  drawPageTitle("ĐÃ GHI NHẬN", C_OK);
  char qty[40]; snprintf(qty, sizeof(qty), "Đạt: %d", demoGoodQty);
  drawCenteredTextFit(qty, SCREEN_LEFT_MARGIN, 156, 216, 36, FONT_SECTION, FONT_SECTION, 1, C_TEXT, true);
}

static String maintenancePage() {
  String page; page.reserve(2200);
  const String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "CHƯA KẾT NỐI";
  const String host = mdnsReady ? String(mdnsHostname) + ".local" : "KHÔNG SẴN SÀNG";
  page += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>MESFlow Maintenance</title><style>body{font-family:Arial;background:#07111f;color:#fff;margin:0;padding:18px}.c{max-width:560px;margin:auto}input,button{width:100%;box-sizing:border-box;padding:12px;margin:6px 0 14px;font-size:16px}button{background:#2878d0;color:#fff;border:0}small{color:#9fb0c5}code{word-break:break-all}</style></head><body><div class='c'>");
  page += F("<h1>MESFlow Kiosk Maintenance</h1><p><small>Device</small><br><code>"); page += htmlEscape(DEVICE_ID);
  const String ssid = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String(WIFI_SSID);
  page += F("</code></p><p><small>WiFi</small><br>"); page += htmlEscape(ssid.c_str());
  page += F("</p><p><small>IP</small><br>"); page += htmlEscape(ip.c_str());
  page += F("</p><p><small>Hostname</small><br>"); page += htmlEscape(host.c_str());
  page += F("</p><p><small>Firmware</small><br>"); page += htmlEscape(APP_VERSION);
  page += F("</p><hr><label>MESFlow API URL</label><input id='url' value='"); page += htmlEscape(SERVER_BASE);
  page += F("'><label>Deploy Agent OTA URL</label><input id='ota' value='"); page += htmlEscape(OTA_AGENT_BASE);
  page += F("'><button onclick='saveUrl()'>Luu API URL</button><pre id='s'></pre><script>async function saveUrl(){let s=document.getElementById('s');try{let r=await fetch('/maintenance/api-url',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({server_url:document.getElementById('url').value,ota_agent_url:document.getElementById('ota').value})});let j=await r.json();s.textContent=j.message||JSON.stringify(j)}catch(e){s.textContent=String(e)}}</script></div></body></html>");
  return page;
}

static void handleMaintenanceApiUrl() {
  if (!maintenanceMode) { maintenanceWeb.send(403, "application/json", "{\"ok\":false}"); return; }
  DynamicJsonDocument doc(512); String body;
  if (deserializeJson(doc, maintenanceWeb.arg("plain"))) {
    maintenanceWeb.send(400, "application/json", "{\"ok\":false,\"message\":\"JSON khong hop le\"}"); return;
  }
  String url = doc["server_url"] | ""; url.trim();
  String otaUrl = doc["ota_agent_url"] | ""; otaUrl.trim();
  if (!validServerBase(url)) {
    maintenanceWeb.send(400, "application/json", "{\"ok\":false,\"message\":\"API URL khong hop le\"}"); return;
  }
  safeCopy(SERVER_BASE, sizeof(SERVER_BASE), url.c_str());
  if (otaUrl.length() == 0 || validServerBase(otaUrl)) safeCopy(OTA_AGENT_BASE, sizeof(OTA_AGENT_BASE), otaUrl.c_str());
  const bool saved = saveDeviceConfig();
  body = saved ? "{\"ok\":true,\"message\":\"Da luu API URL\"}" : "{\"ok\":false,\"message\":\"Khong luu duoc\"}";
  maintenanceWeb.send(saved ? 200 : 500, "application/json", body);
}

static void drawMaintenanceScreen() {
  tft.fillScreen(C_BG); drawIndustrialHeader(WiFi.status() == WL_CONNECTED ? C_OK : C_ERR);
  drawSectionLabel("MESFLOW");
  drawCenteredTextFit("BẢO TRÌ", SCREEN_LEFT_MARGIN, 54, 216, 28, FONT_TITLE, FONT_TITLE - 1, 1, C_TEXT, true);
  drawTextBox("WI-FI", 18, 86, 42, 20, FONT_HEADER, FONT_HEADER, 1, TextAlign::LEFT, C_MUTED, true);
  const String ssid = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String(WIFI_SSID);
  drawTextBox(ssid.c_str(), 66, 83, 156, 24, FONT_SECTION, FONT_SECTION - 1, 1, TextAlign::LEFT, C_TEXT, true);
  drawTextBox("IP", 18, 112, 34, 20, FONT_HEADER, FONT_HEADER, 1, TextAlign::LEFT, C_MUTED, true);
  const String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "CHƯA KẾT NỐI";
  drawTextBox(ip.c_str(), 54, 108, 168, 24, FONT_SECTION, FONT_SECTION - 1, 1, TextAlign::LEFT, C_INFO, true);
  drawTextBox("WEB", 18, 140, 42, 20, FONT_HEADER, FONT_HEADER, 1, TextAlign::LEFT, C_MUTED, true);
  const String web = mdnsReady ? String(mdnsHostname) + ".local" : "KHÔNG SẴN SÀNG";
  drawTextBox(web.c_str(), 62, 137, 160, 22, FONT_HEADER, FONT_HEADER, 1, TextAlign::LEFT, C_TEXT, true);
  drawTextBox("TRẠNG THÁI",18,170,90,20,FONT_HEADER,FONT_HEADER,1,TextAlign::LEFT,C_MUTED,true);
  // TEMP DIAGNOSTIC (2026-08-22): offlineMode==false does not by itself mean
  // OP/worker scans take the online path -- that gate also checks
  // serverLinkState (WIFI_DOWN/UNREACHABLE), which had no visible readout
  // anywhere, so a queue-is-empty kiosk could still be silently forced into
  // offline-only cache lookups (error code -44) with nothing on screen
  // explaining why. Surface it here -- remove once root-caused.
  const char* linkText = offlineMode ? "OFFLINE" :
      serverLinkState == ServerLinkState::WIFI_DOWN ? "WIFIDOWN" :
      serverLinkState == ServerLinkState::UNREACHABLE ? "UNREACH" :
      serverLinkState == ServerLinkState::UNKNOWN ? "UNKNOWN" : "ONLINE";
  const uint16_t linkColor = offlineMode || serverLinkState == ServerLinkState::WIFI_DOWN ||
      serverLinkState == ServerLinkState::UNREACHABLE ? C_WARN :
      serverLinkState == ServerLinkState::UNKNOWN ? C_MUTED : C_OK;
  drawTextBox(linkText,112,167,110,22,FONT_SECTION,FONT_SECTION,1,TextAlign::LEFT,linkColor,true);
  char pendingText[16];snprintf(pendingText,sizeof(pendingText),"%u",maintenancePendingOverride>=0?(unsigned)maintenancePendingOverride:(unsigned)countPendingOfflineEvents());
  drawTextBox("CHỜ ĐỒNG BỘ",18,201,110,20,FONT_HEADER,FONT_HEADER,1,TextAlign::LEFT,C_MUTED,true);
  drawTextBox(pendingText,150,198,72,22,FONT_SECTION,FONT_SECTION,1,TextAlign::LEFT,C_TEXT,true);
  char syncTime[12]="--:--";if(lastOfflineSyncEpoch){time_t raw=lastOfflineSyncEpoch;struct tm info;if(localtime_r(&raw,&info))snprintf(syncTime,sizeof(syncTime),"%02d:%02d",info.tm_hour,info.tm_min);}
  drawTextBox("SYNC CUỐI",18,222,90,18,FONT_HEADER,FONT_HEADER,1,TextAlign::LEFT,C_MUTED,true);
  drawTextBox(syncTime,150,219,72,20,FONT_HEADER,FONT_HEADER,1,TextAlign::LEFT,C_TEXT,true);
  drawTextBox("1  WI-FI",18,246,92,22,FONT_OPTION,FONT_OPTION,1,TextAlign::LEFT,C_TEXT,true);
  drawTextBox("2  ĐỒNG BỘ",120,246,102,22,FONT_OPTION,FONT_OPTION,1,TextAlign::LEFT,C_TEXT,true);
  drawFooterTwoActions("", "# THOÁT");
  safeCopy(maintenanceLastIp, sizeof(maintenanceLastIp), ip.c_str());
}

static void redrawProductionUi() {
  switch (uiState) {
    case UiState::READY: drawReady(); break;
    case UiState::WORKER_OK: drawWorker(); break;
    case UiState::OPERATION_OK: drawOperation(); break;
    case UiState::START_SUCCESS: drawStartSuccess(); break;
    case UiState::INPUT_GOOD: drawQtyInput("SẢN PHẨM ĐẠT", demoGoodQty); break;
    case UiState::INPUT_DEFECT: drawQtyInput("SẢN PHẨM LỖI", demoDefectQty); break;
    case UiState::ASK_REWORK: drawAskRework(); break;
    case UiState::INPUT_REWORK: drawQtyInput("LỖI SỬA ĐƯỢC", demoReworkQty); break;
    case UiState::CONFIRM_QTY: drawWorkerQtyConfirmation(); break;
    case UiState::FINISH_RETRY: drawFinishRetry(); break;
    case UiState::ERROR_STATE: drawError(); break;
    default: drawReady(); break;
  }
}

static void enterMaintenanceMode(bool duringBoot) {
  if (maintenanceMode) return;
  maintenanceMode = true; maintenanceEnteredDuringBoot = duringBoot;
  wifiSetupHoldTriggered = true;
  static bool routesRegistered = false;
  if (!routesRegistered) {
    auto serveMaintenancePage = [](){ maintenanceWeb.send(200, "text/html; charset=utf-8", maintenancePage()); };
    maintenanceWeb.on("/", HTTP_GET, serveMaintenancePage);
    maintenanceWeb.on("/maintenance", HTTP_GET, serveMaintenancePage);
    maintenanceWeb.on("/maintenance/api-url", HTTP_POST, handleMaintenanceApiUrl);
    routesRegistered = true;
  }
  if (WiFi.status() == WL_CONNECTED) {
    maintenanceWeb.begin();
    maintenanceWebActive = true;
    if (mdnsReady) MDNS.addService("http", "tcp", 80);
  }
  drawMaintenanceScreen();
}

static void exitMaintenanceMode() {
  if (!maintenanceMode) return;
  maintenanceWeb.stop();
  maintenanceWebActive = false;
  maintenanceMode = false; maintenanceEnteredDuringBoot = false;
  wifiSetupHoldActive = false; wifiSetupHoldTriggered = false; wifiSetupHoldStartedAt = 0;
  redrawProductionUi();
}

static void serviceMaintenanceMode() {
  if (!maintenanceMode) return;
  if (WiFi.status() == WL_CONNECTED && !maintenanceWebActive) {
    maintenanceWeb.begin(); maintenanceWebActive = true;
    if (mdnsReady) MDNS.addService("http", "tcp", 80);
  } else if (WiFi.status() != WL_CONNECTED && maintenanceWebActive) {
    maintenanceWeb.stop(); maintenanceWebActive = false;
  }
  if (maintenanceWebActive) maintenanceWeb.handleClient();
  if (millis() - lastMaintenanceRefreshAt >= 1000) {
    lastMaintenanceRefreshAt = millis();
    const String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "CHƯA KẾT NỐI";
    if (ip != maintenanceLastIp) drawMaintenanceScreen();
  }
}

#if MESFLOW_UI_SCREENSHOT
static bool debugAccessAllowed() { return maintenanceMode || MESFLOW_UI_SCREENSHOT; }
static char debugScreenName[40] = "live";
static char debugStateName[32] = "";
static int debugGoodQty = 22;
static int debugDefectQty = 3;
static int debugRepairQty = 1;

static const char* const DEBUG_SCREENS[] = {
  "ready", "scan_employee", "employee_ok", "scan_operation", "operation_ok",
  "working", "input_good", "input_defect", "ask_rework", "input_rework",
  "confirm_qty", "finish_success", "error_state",
  "employee_short_name", "employee_long_name", "operation_short_name",
  "operation_long_name", "confirm_normal", "confirm_large_numbers",
  "repairable_zero", "repairable_nonzero", "error_short", "error_long",
  "offline", "offline_employee", "offline_operation", "offline_input_good",
  "offline_input_defect", "offline_repairable", "offline_confirm",
  "offline_saved", "storage_warning", "maintenance"
};

static void setDebugIdentity(bool longText) {
  safeCopy(rt.workerName, sizeof(rt.workerName), longText ? "NGUYEN HOANG MINH PHUONG" : "NGUYEN VAN AN");
  safeCopy(rt.workerCode, sizeof(rt.workerCode), "NV-QA-001");
  safeCopy(rt.operationName, sizeof(rt.operationName), longText
      ? "GIA CONG VA DAN KEO CHO THUNG CARTON THANH PHAM"
      : "DAN KEO CHO THUNG CARTON");
  safeCopy(rt.po, sizeof(rt.po), longText ? "MESFLOW-PO-260809-TEST-LONG-001" : "PO-260809-001");
  safeCopy(rt.part, sizeof(rt.part), "KHUNG MAY");
  rt.online = true;
}

static bool renderDebugScreen(const String& requested) {
  RuntimeData savedRt = rt;
  const int savedGood = demoGoodQty, savedDefect = demoDefectQty, savedRework = demoReworkQty;
  const bool longText = requested.indexOf("long") >= 0;
  setDebugIdentity(longText);
  demoGoodQty = requested == "confirm_large_numbers" ? 999999 : 22;
  demoDefectQty = requested == "confirm_large_numbers" ? 9999 : 3;
  demoReworkQty = requested == "repairable_zero" ? 0 : 1;
  debugGoodQty = demoGoodQty; debugDefectQty = demoDefectQty; debugRepairQty = demoReworkQty;

  const char* state = nullptr;
  if (requested == "ready" || requested == "scan_employee") { state = "READY"; drawReady(); }
  else if (requested == "employee_ok" || requested == "scan_operation" || requested == "employee_short_name" || requested == "employee_long_name") { state = "WORKER_OK"; drawWorker(); }
  else if (requested == "operation_ok" || requested == "operation_short_name" || requested == "operation_long_name") { state = "OPERATION_OK"; drawOperation(); }
  else if (requested == "working") { state = "START_SUCCESS"; drawStartSuccess(); }
  else if (requested == "input_good") { state = "INPUT_GOOD"; drawQtyInput("SẢN PHẨM ĐẠT", demoGoodQty); }
  else if (requested == "input_defect") { state = "INPUT_DEFECT"; drawQtyInput("SẢN PHẨM LỖI", demoDefectQty); }
  else if (requested == "ask_rework" || requested == "repairable_zero" || requested == "repairable_nonzero") { state = "ASK_REWORK"; drawAskRework(); }
  else if (requested == "input_rework") { state = "INPUT_REWORK"; drawQtyInput("LỖI SỬA ĐƯỢC", demoReworkQty); }
  else if (requested == "confirm_qty" || requested == "confirm_normal" || requested == "confirm_large_numbers") { state = "CONFIRM_QTY"; drawWorkerQtyConfirmation(); }
  else if (requested == "finish_success") { state = "FINISH_SUCCESS"; drawWorkerFinishSuccess(); }
  else if (requested == "offline") {
    state = "OFFLINE"; rt.online = false;
    drawLoading("MẤT KẾT NỐI", "ĐANG THỬ LẠI");
  }
  else if (requested == "offline_employee") {state="OFFLINE_WORKER";rt.online=false;drawWorker();}
  else if (requested == "offline_operation") {state="OFFLINE_OPERATION";rt.online=false;drawOperation();}
  else if (requested == "offline_input_good") {state="OFFLINE_INPUT_GOOD";rt.online=false;drawQtyInput("SẢN PHẨM ĐẠT",demoGoodQty);}
  else if (requested == "offline_input_defect") {state="OFFLINE_INPUT_DEFECT";rt.online=false;drawQtyInput("SẢN PHẨM LỖI",demoDefectQty);}
  else if (requested == "offline_repairable") {state="OFFLINE_REPAIRABLE";rt.online=false;drawQtyInput("LỖI SỬA ĐƯỢC",demoReworkQty);}
  else if (requested == "offline_confirm") {state="OFFLINE_CONFIRM";rt.online=false;drawWorkerQtyConfirmation();}
  else if (requested == "offline_saved") {state="OFFLINE_SAVED";rt.online=false;drawSimple("ĐÃ LƯU TẠM","SẼ TỰ ĐỘNG","ĐỒNG BỘ","",C_WARN);}
  else if (requested == "storage_warning") {state="STORAGE_WARNING";rt.online=false;drawSimple("BỘ NHỚ GẦN ĐẦY","BÁO QUẢN LÝ","KHÔNG TẮT MÁY","* QUAY LẠI",C_ERR);}
  else if (requested == "maintenance") { state = "MAINTENANCE";maintenancePendingOverride=17;drawMaintenanceScreen();maintenancePendingOverride=-1; }
  else if (requested == "error_state" || requested == "error_short" || requested == "error_long") {
    state = "ERROR_STATE";
    safeCopy(rt.lastError, sizeof(rt.lastError), requested == "error_long"
        ? "CHUA CO SAN PHAM DAU VAO CHO CONG DOAN NAY"
        : "CHUA CO SAN PHAM DAU VAO");
    drawError();
  }
  if (state) {
    safeCopy(debugScreenName, sizeof(debugScreenName), requested.c_str());
    safeCopy(debugStateName, sizeof(debugStateName), state);
  }
  rt = savedRt; demoGoodQty = savedGood; demoDefectQty = savedDefect; demoReworkQty = savedRework;
  return state != nullptr;
}

static void putLe16(uint8_t* out, uint16_t value) {
  out[0] = value & 0xFF; out[1] = value >> 8;
}

static void putLe32(uint8_t* out, uint32_t value) {
  out[0] = value & 0xFF; out[1] = (value >> 8) & 0xFF;
  out[2] = (value >> 16) & 0xFF; out[3] = value >> 24;
}

// Reads the actual ILI9341 display RAM (RAMRD 0x2E) and converts its 18-bit
// RGB response directly to one BGR888 BMP row. No full-screen framebuffer.
static void readLcdBmpRow(uint16_t y, uint8_t* bgr) {
  tft.setAddrWindow(0, y, SW, 1);
  tft.startWrite();
  tft.writeCommand(0x2E);
  (void)tft.spiRead();
  for (int16_t x = 0; x < SW; ++x) {
    const uint8_t r = tft.spiRead(), g = tft.spiRead(), b = tft.spiRead();
    bgr[x * 3] = b; bgr[x * 3 + 1] = g; bgr[x * 3 + 2] = r;
  }
  tft.endWrite();
}

static void readShadowBmpRow(uint16_t y, uint8_t* bgr) {
  const uint16_t* source = uiShadowFramebuffer + static_cast<size_t>(y) * SW;
  for (int16_t x = 0; x < SW; ++x) {
    const uint16_t c = source[x];
    bgr[x * 3] = static_cast<uint8_t>((c & 0x1F) * 255 / 31);
    bgr[x * 3 + 1] = static_cast<uint8_t>(((c >> 5) & 0x3F) * 255 / 63);
    bgr[x * 3 + 2] = static_cast<uint8_t>(((c >> 11) & 0x1F) * 255 / 31);
  }
}

static void handleDebugScreenshot() {
  if (!debugAccessAllowed()) { provisionWeb.send(403, "text/plain", "Forbidden"); return; }
  constexpr uint32_t rowBytes = SW * 3U;
  constexpr uint32_t imageBytes = rowBytes * SH;
  constexpr uint32_t fileBytes = 54U + imageBytes;
  uint8_t header[54] = {};
  header[0] = 'B'; header[1] = 'M'; putLe32(header + 2, fileBytes);
  putLe32(header + 10, 54); putLe32(header + 14, 40);
  putLe32(header + 18, SW); putLe32(header + 22, SH);
  putLe16(header + 26, 1); putLe16(header + 28, 24);
  putLe32(header + 34, imageBytes); putLe32(header + 38, 2835); putLe32(header + 42, 2835);
  provisionWeb.setContentLength(fileBytes);
  provisionWeb.send(200, "image/bmp", "");
  WiFiClient client = provisionWeb.client();
  client.write(header, sizeof(header));
  uint8_t row[rowBytes];
  if (!uiShadowFramebuffer) tft.setSPISpeed(6000000);
  for (int16_t y = SH - 1; y >= 0 && client.connected(); --y) {
    if (uiShadowFramebuffer) readShadowBmpRow(y, row); else readLcdBmpRow(y, row);
    client.write(row, sizeof(row));
    if ((y & 15) == 0) delay(0);
  }
  if (!uiShadowFramebuffer) tft.setSPISpeed(20000000);
}

static void handleDebugUiState() {
  if (!debugAccessAllowed()) { provisionWeb.send(403, "application/json", "{\"error\":\"forbidden\"}"); return; }
  DynamicJsonDocument doc(1024);
  doc["screen"] = debugScreenName;
  doc["state"] = debugStateName[0] ? debugStateName : stateName(uiState);
  doc["width"] = SW; doc["height"] = SH; doc["version"] = APP_VERSION; doc["online"] = rt.online;
  doc["employee"] = debugScreenName[0] && strcmp(debugScreenName, "live") != 0 ? "QA DEMO" : rt.workerName;
  doc["operation"] = debugScreenName[0] && strcmp(debugScreenName, "live") != 0 ? "QA UI CAPTURE" : rt.operationName;
  doc["good_qty"] = debugGoodQty; doc["defect_qty"] = debugDefectQty;
  doc["repair_qty"] = debugRepairQty; doc["scrap_qty"] = max(debugDefectQty - debugRepairQty, 0);
  String body; serializeJson(doc, body); provisionWeb.send(200, "application/json", body);
}

static void handleDebugScreens() {
  if (!debugAccessAllowed()) { provisionWeb.send(403, "application/json", "{\"error\":\"forbidden\"}"); return; }
  DynamicJsonDocument doc(2048); JsonArray screens = doc.to<JsonArray>();
  for (const char* screen : DEBUG_SCREENS) screens.add(screen);
  String body; serializeJson(doc, body); provisionWeb.send(200, "application/json", body);
}

static void handleDebugShowScreen() {
  if (!debugAccessAllowed()) { provisionWeb.send(403, "application/json", "{\"error\":\"forbidden\"}"); return; }
  if (!provisionWeb.hasArg("screen")) { provisionWeb.send(400, "application/json", "{\"ok\":false,\"error\":\"screen required\"}"); return; }
  const String requested = provisionWeb.arg("screen");
  if (!renderDebugScreen(requested)) { provisionWeb.send(404, "application/json", "{\"ok\":false,\"error\":\"unknown screen\"}"); return; }
  provisionWeb.send(200, "application/json", String("{\"ok\":true,\"screen\":\"") + requested + "\"}");
}
#endif

void drawReturnCountdown(uint32_t holdMs) {
  uint32_t elapsed = millis() - stateEnteredAt;
  if (elapsed > holdMs) elapsed = holdMs;
  uint32_t remainMs = holdMs - elapsed;

  // Chỉ hiển thị thanh tiến trình, không thêm chữ nhỏ gây rối.
  const int16_t barX = 24;
  const int16_t barY = 270;
  const int16_t barW = SW - 48;
  const int16_t barH = 8;
  tft.fillRect(barX - 1, barY - 1, barW + 2, barH + 2, C_PANEL);
  tft.drawRect(barX, barY, barW, barH, C_MUTED);
  int16_t fillW = static_cast<int16_t>((uint64_t)(barW - 2) * remainMs / holdMs);
  if (fillW > 0) tft.fillRect(barX + 1, barY + 1, fillW, barH - 2, C_OK);
}

void beginSessionTrace() {
  uint32_t r = esp_random() & 0xFFFF;
  time_t now = time(nullptr);
  uint32_t stamp = now >= MIN_VALID_EPOCH ? static_cast<uint32_t>(now) : millis() / 1000UL;
  snprintf(sessionTraceId, sizeof(sessionTraceId), "%s-%lu-%04lX",
           DEVICE_ID, static_cast<unsigned long>(stamp), static_cast<unsigned long>(r));
}

void endSessionTrace() {
  sessionTraceId[0] = '\0';
}

void nextClientEventId(char* out, size_t outSize) {
  ++clientEventCounter;
  snprintf(out, outSize, "%s-%08lu", DEVICE_ID, static_cast<unsigned long>(clientEventCounter));
  if ((clientEventCounter & 0x0F) == 0) {
    prefs.begin("mesflow", false);
    prefs.putULong("evt_counter", clientEventCounter);
    prefs.end();
  }
}

bool postActionPayload(const String& payload) {
  if (!rt.bound || WiFi.status() != WL_CONNECTED) return false;
  MesHttpSession net;
  char url[320];
  snprintf(url, sizeof(url), "%s/api/kiosk/events", SERVER_BASE);
  if (!net.begin(url, 1200, 2500, 3)) return false;
  HTTPClient& http = net.http();
  addJsonHeaders(http);
  addAuthHeaders(http);
  int status = http.POST(payload);
  net.end();
  // Deliberately do not call setError(): telemetry must never change kiosk UI.
  return status >= 200 && status < 300;
}

uint16_t actionQueueCount() {
  if (!LittleFS.exists(ACTION_QUEUE_PATH)) return 0;
  File f = LittleFS.open(ACTION_QUEUE_PATH, "r");
  if (!f) return 0;
  uint16_t count = 0;
  while (f.available()) { String line = f.readStringUntil('\n'); if (line.length()) ++count; }
  f.close();
  return count;
}

bool enqueueActionPayload(const String& payload) {
  if (!fsReady) return false;
  // Keep the newest ACTION_QUEUE_MAX events. Rewrite only when at capacity.
  uint16_t count = actionQueueCount();
  if (count >= ACTION_QUEUE_MAX) {
    File in = LittleFS.open(ACTION_QUEUE_PATH, "r");
    File out = LittleFS.open(ACTION_QUEUE_TMP_PATH, "w");
    if (!in || !out) { if (in) in.close(); if (out) out.close(); return false; }
    bool skipped = false;
    while (in.available()) {
      String line = in.readStringUntil('\n');
      if (!line.length()) continue;
      if (!skipped) { skipped = true; ++actionQueueDropped; continue; }
      out.println(line);
    }
    in.close(); out.close();
    LittleFS.remove(ACTION_QUEUE_PATH);
    LittleFS.rename(ACTION_QUEUE_TMP_PATH, ACTION_QUEUE_PATH);
  }
  File f = LittleFS.open(ACTION_QUEUE_PATH, "a");
  if (!f) return false;
  bool ok = f.println(payload) > 0;
  f.close();
  return ok;
}

bool emitActionEvent(const char* eventType, const char* category,
                     const char* result, int httpStatus,
                     uint32_t durationMs, const char* message,
                     const char* errorCode, const char* inputType) {
  DynamicJsonDocument event(2048);
  char eventId[112]; nextClientEventId(eventId, sizeof(eventId));
  event["client_event_id"] = eventId;
  event["session_trace_id"] = sessionTraceId;
  event["device_id"] = DEVICE_ID;
  event["device_name"] = DEVICE_NAME;
  event["station_code"] = STATION_CODE;
  event["firmware"] = APP_VERSION;
  event["event_type"] = eventType;
  event["event_category"] = category;
  event["ui_state"] = stateName(uiState);
  if (result && result[0]) event["event_result"] = result;
  if (httpStatus) event["http_status"] = httpStatus;
  if (durationMs) event["action_duration_ms"] = durationMs;
  if (message && message[0]) event["message"] = message;
  if (errorCode && errorCode[0]) event["error_code"] = errorCode;
  if (inputType && inputType[0]) event["input_type"] = inputType;
  event["worker_code"] = rt.hasWorker ? rt.workerCode : "";
  event["operation_code"] = rt.hasOperation ? (rt.operationCode[0] ? rt.operationCode : rt.operationQr) : "";
  event["operation_name"] = rt.hasOperation ? rt.operationName : "";
  event["po_code"] = rt.hasOperation ? rt.po : "";
  event["session_id"] = rt.activeSessionId;
  String payload; serializeJson(event, payload);
  // Telemetry is intentionally queue-only. Sending telemetry synchronously from
  // the scanner/quantity path created two or three back-to-back HTTP sockets
  // around FINISH. On some ESP32-S3 boards that caused a brief Wi-Fi state
  // transition, socket exhaustion, or a power-current spike that looked like a
  // Wi-Fi reconnect. Business API traffic always has priority.
  return enqueueActionPayload(payload);
}

void serviceActionEventQueue() {
  if (!fsReady || !rt.bound || WiFi.status() != WL_CONNECTED) return;
  if (hasPendingTransaction()) return;
  if (uiState == UiState::STARTING || uiState == UiState::FINISHING ||
      uiState == UiState::SYNC_PENDING || uiState == UiState::LOOKUP_WORKER ||
      uiState == UiState::LOOKUP_OPERATION) return;
  if (millis() - lastActionQueueRetryAt < ACTION_QUEUE_RETRY_MS) return;
  lastActionQueueRetryAt = millis();
  if (!LittleFS.exists(ACTION_QUEUE_PATH)) return;

  // Send at most one telemetry event per pass. Never drain the entire file in
  // one loop because repeated POST/open/close cycles can starve the scanner UI
  // and destabilize the Wi-Fi driver immediately after a quantity submission.
  File in = LittleFS.open(ACTION_QUEUE_PATH, "r");
  File out = LittleFS.open(ACTION_QUEUE_TMP_PATH, "w");
  if (!in || !out) { if (in) in.close(); if (out) out.close(); return; }

  bool firstPendingHandled = false;
  bool sendSucceeded = false;
  while (in.available()) {
    String line = in.readStringUntil('\n');
    if (!line.length()) continue;
    if (!firstPendingHandled) {
      firstPendingHandled = true;
      sendSucceeded = postActionPayload(line);
      if (sendSucceeded) continue;
    }
    out.println(line);
  }
  in.close();
  out.close();
  LittleFS.remove(ACTION_QUEUE_PATH);
  if (LittleFS.exists(ACTION_QUEUE_TMP_PATH)) {
    File check = LittleFS.open(ACTION_QUEUE_TMP_PATH, "r");
    const bool hasRemaining = check && check.size() > 0;
    if (check) check.close();
    if (hasRemaining) LittleFS.rename(ACTION_QUEUE_TMP_PATH, ACTION_QUEUE_PATH);
    else LittleFS.remove(ACTION_QUEUE_TMP_PATH);
  }
}

uint32_t maxStateDurationMs(UiState state) {
  switch (state) {
    case UiState::BOOT: return 30000;
    case UiState::WIFI: return 25000;
    case UiState::BINDING: return 12000;
    case UiState::LOOKUP_WORKER:
    case UiState::LOOKUP_OPERATION: return 10000;
    case UiState::OPERATION_OK: return 12000;
    case UiState::STARTING:
    case UiState::FINISHING: return 10000;
    case UiState::FINISH_RETRY: return QUANTITY_CONFIRM_IDLE_TIMEOUT_MS;
    case UiState::START_SUCCESS: return START_SUCCESS_HOLD_MS + 3000;
    case UiState::FINISH_SUCCESS: return 10000;
    case UiState::ERROR_STATE: return BUSINESS_ERROR_HOLD_MS + 4000;
    case UiState::SYNC_PENDING: return 20000;
    case UiState::OFFLINE: return 30000;
    // Cho nhap san luong khong duoc chiem kiosk vo han.
    // Timeout chi giai phong giao dien; session san xuat tren server van mo.
    case UiState::INPUT_GOOD:
    case UiState::ASK_REWORK:
    case UiState::INPUT_REWORK:
    case UiState::INPUT_DEFECT: return QUANTITY_INPUT_IDLE_TIMEOUT_MS;
    case UiState::CONFIRM_QTY: return QUANTITY_CONFIRM_IDLE_TIMEOUT_MS;
    // Cac man hinh cho quet/chay session co the cho vo han.
    case UiState::READY:
    case UiState::WORKER_OK:
    case UiState::ACTIVE_SESSION:
    case UiState::TOUCH_TEST:
    default: return 0;
  }
}

bool sendKioskEvent(const char* eventType, const char* severity, const char* message,
                    const char* recoveryAction, uint32_t stateAgeMs) {
  if (!rt.bound || WiFi.status() != WL_CONNECTED) return false;
  DynamicJsonDocument request(1536);
  request["device_id"] = DEVICE_ID;
  request["device_name"] = DEVICE_NAME;
  request["station_code"] = STATION_CODE;
  request["firmware"] = APP_VERSION;
  request["event_type"] = eventType;
  request["event_category"] = "SYSTEM";
  request["session_trace_id"] = sessionTraceId;
  char technicalEventId[112]; nextClientEventId(technicalEventId, sizeof(technicalEventId));
  request["client_event_id"] = technicalEventId;
  request["severity"] = severity;
  request["message"] = message;
  request["ui_state"] = stateName(uiState);
  request["state_age_seconds"] = stateAgeMs / 1000UL;
  request["last_user_action_seconds"] = lastUserActionAt ? (millis() - lastUserActionAt) / 1000UL : 0;
  request["queue_size"] = countPendingOfflineEvents() + actionQueueCount() + (hasPendingTransaction() ? 1 : 0);
  request["pending_transaction"] = hasPendingTransaction();
  request["worker_code"] = rt.hasWorker ? rt.workerCode : "";
  request["operation_name"] = rt.hasOperation ? rt.operationName : "";
  request["po_code"] = rt.hasOperation ? rt.po : "";
  request["po_code"] = rt.hasOperation ? rt.po : "";
  request["http_status"] = rt.lastHttpStatus;
  request["last_error"] = rt.lastError;
  request["wifi_rssi"] = WiFi.RSSI();
  request["free_heap"] = ESP.getFreeHeap();
  request["recovery_action"] = recoveryAction;
  request["recovery_count"] = stateRecoveryCount;

  DynamicJsonDocument response(256);
  return httpPostJson("/api/kiosk/events", request, response, true, true);
}

void recoverUiFromStuck(const char* reason, bool userRequested) {
  const uint32_t age = millis() - stateEnteredAt;
  char stuckState[32];
  safeCopy(stuckState, sizeof(stuckState), stateName(uiState));
  ++stateRecoveryCount;
  lastRecoveryAt = millis();
  safeCopy(lastRecoveryReason, sizeof(lastRecoveryReason), reason);
  Serial.printf("[STATE WATCHDOG] state=%s age=%lu reason=%s pending=%s\n",
                stuckState, static_cast<unsigned long>(age), reason,
                hasPendingTransaction() ? "YES" : "NO");
  remoteLogf("STATE WATCHDOG state=%s age=%lu reason=%s", stuckState,
             static_cast<unsigned long>(age), reason);
  const bool quantityAbandoned =
      uiState == UiState::INPUT_GOOD || uiState == UiState::INPUT_REWORK || uiState == UiState::INPUT_DEFECT ||
      uiState == UiState::CONFIRM_QTY;
  const char* eventType = userRequested ? "USER_FORCED_EXIT"
                         : (quantityAbandoned ? "ABANDONED_QTY_ENTRY" : "STUCK_STATE");
  const char* severity = userRequested || quantityAbandoned ? "WARNING" : "CRITICAL";
  const char* eventMessage = quantityAbandoned ? "QUANTITY_ENTRY_TIMEOUT" : reason;
  const char* recoveryAction = quantityAbandoned
      ? "RELEASE_KIOSK_KEEP_SERVER_SESSION"
      : "RETURN_TO_SCAN_EMPLOYEE";
  sendKioskEvent(eventType, severity, eventMessage, recoveryAction, age);
  emitActionEvent(userRequested ? "USER_CANCELLED" : "STATE_TIMEOUT", "USER_ACTION",
                  "RECOVERED", rt.lastHttpStatus, age, eventMessage);

  // Never delete a durable START/FINISH transaction here. It will continue to
  // synchronize in the background. Only clear temporary screen selections.
  clearRuntimeSelection();
  returnToReadyAfterError = false;
  rt.lastHttpStatus = 0;
  rt.lastError[0] = '\0';
  // Return the operator to a usable screen even when a durable transaction is
  // still queued. maintainConnection() keeps retrying it in the background.
  setUi(UiState::READY);
  emitActionEvent("RETURN_TO_READY", "UI_STATE", "SUCCESS");
  endSessionTrace();
}

void serviceStateWatchdog() {
  if (millis() - lastStateWatchdogAt < STATE_WATCHDOG_CHECK_MS) return;
  lastStateWatchdogAt = millis();
  const uint32_t limit = maxStateDurationMs(uiState);
  if (!limit) return;
  const uint32_t age = millis() - stateEnteredAt;
  if (age > limit) recoverUiFromStuck("UI_STATE_TIMEOUT", false);
}

void drawPendingSync() {
  const bool isFinish = pendingTx.type == static_cast<uint8_t>(PendingType::FINISH);
  const char* action = isFinish ? "KẾT THÚC" : "BẮT ĐẦU";
  const char* detail = WiFi.status() == WL_CONNECTED ? "ĐANG GỬI" : "CHỜ KẾT NỐI";
  drawSimple("ĐÃ LƯU TẠM", action, detail, "TỰ ĐỘNG THỬ LẠI", C_HEADER);
}

void drawError() {
  char shortError[42];
  safeCopy(shortError, sizeof(shortError), rt.lastError);
  tft.fillScreen(C_BG);
  drawIndustrialHeader(C_ERR);
  drawPageTitle("LỖI", C_ERR);
  drawCenteredTextFit(shortError, SCREEN_LEFT_MARGIN, 120, 216, 92, FONT_SECTION, FONT_SECTION - 1, 3, C_TEXT, true);
  // TEMP DIAGNOSTIC (2026-08-22): "MAT KET NOI MAY CHU" collapses many
  // different underlying failures (WiFi not associated, TCP connect refused,
  // DNS failure, read timeout...) into one friendly message, which made this
  // exact bug impossible to pin down remotely. Surface the raw code so the
  // next occurrence is diagnosable from a photo -- remove once root-caused.
  char diag[24];
  snprintf(diag, sizeof(diag), "code %d", rt.lastHttpStatus);
  printCentered(diag, SW / 2, 214, 1, C_MUTED, true);
  // TEMP DIAGNOSTIC (2026-08-22, round 2): code -44 alone doesn't say WHY the
  // OP-scan gate picked the offline-cache-only path instead of asking the
  // server. Capture the exact gate inputs atomically at the moment of the
  // error, since serverLinkState/offlineMode can change again before anyone
  // can check the Maintenance screen after the fact.
  char diag2[40];
  const char* lt = serverLinkState == ServerLinkState::WIFI_DOWN ? "WD" :
      serverLinkState == ServerLinkState::UNREACHABLE ? "UR" :
      serverLinkState == ServerLinkState::UNKNOWN ? "UK" : "AV";
  snprintf(diag2, sizeof(diag2), "om=%d link=%s wifi=%d", offlineMode ? 1 : 0, lt,
           static_cast<int>(WiFi.status()));
  printCentered(diag2, SW / 2, 230, 1, C_MUTED, true);
  // TEMP DIAGNOSTIC (2026-08-22, round 3): whichever setError() shows on
  // screen overwrites rt.lastHttpStatus, hiding the actual background
  // failure (heartbeat/sync POST) that degraded serverLinkState in the
  // first place. lastLinkFailureStatus/Source is a separate, dedicated
  // record of THAT failure, updated only inside recordServerResult().
  char diag3[40];
  snprintf(diag3, sizeof(diag3), "last-fail %s=%d", lastLinkFailureSource, lastLinkFailureStatus);
  printCentered(diag3, SW / 2, 246, 1, C_MUTED, true);
  // TEMP DIAGNOSTIC (2026-08-22, round 4): "GET=-2" means http.begin()
  // itself returned false. Per the ESP32 core's HTTPClient::begin(client,
  // url) source, that function does ONLY string parsing (checks for ':' and
  // a http/https protocol prefix) -- no DNS/socket I/O happens at that step.
  // It can only fail if the concatenated URL is malformed, which would mean
  // SERVER_BASE was empty/corrupt in RAM at that moment (NVS itself is
  // fine, confirmed separately), or heap pressure is corrupting the String
  // operations inside that call. Surface both directly instead of guessing.
  char diag4[40];
  snprintf(diag4, sizeof(diag4), "srv_len=%d heap=%u", static_cast<int>(strlen(SERVER_BASE)),
           static_cast<unsigned>(ESP.getFreeHeap()));
  printCentered(diag4, SW / 2, 258, 1, C_MUTED, true);
  drawFooter("* QUAY LẠI", "");
}

void setUi(UiState next) {
  const UiState previous = uiState;
  uiState = next;
  stateEnteredAt = millis();

  // Luong sau khi quet Operation duoc toi gian de tranh chop nhieu man hinh:
  // WORKER_OK -> OPERATION_OK (giu nguyen trong luc gui START) -> START_SUCCESS.
  // LOOKUP_OPERATION, STARTING va SYNC_PENDING cua giao dich START chi doi state,
  // khong ve lai toan man hinh, nen nguoi dung khong bi roi mat.
  bool redrawHeaderOnly = false;

  switch (next) {
    case UiState::BOOT: drawLoading("ĐANG KHỞI ĐỘNG", "VUI LÒNG CHỜ"); break;
    case UiState::WIFI: drawLoading("ĐANG KẾT NỐI", "VUI LÒNG CHỜ"); break;
    case UiState::BINDING: drawLoading("ĐANG LIÊN KẾT", "VỚI TRẠM LÀM VIỆC"); break;
    case UiState::READY: drawReady(); break;
    case UiState::LOOKUP_WORKER:
      // Giữ nguyên màn QUÉT THẺ trong lúc lookup; có kết quả thì chuyển thẳng sang nhân viên.
      redrawHeaderOnly = true;
      break;
    case UiState::WORKER_OK: drawWorker(); break;

    case UiState::LOOKUP_OPERATION:
      // Giu man hinh nhan vien trong luc lookup Operation.
      redrawHeaderOnly = true;
      break;

    case UiState::OPERATION_OK:
      // Day la man hinh duy nhat hien trong luc bat dau session.
      drawOperation();
      break;

    case UiState::STARTING:
      // Giu nguyen man hinh Operation, khong chen man "DANG GUI".
      redrawHeaderOnly = true;
      break;

    case UiState::START_SUCCESS: drawStartSuccess(); break;
    case UiState::INPUT_GOOD: drawQtyInput("SẢN PHẨM ĐẠT", demoGoodQty); break;
    case UiState::INPUT_DEFECT: drawQtyInput("SẢN PHẨM LỖI", demoDefectQty); break;
    case UiState::ASK_REWORK: drawAskRework(); break;
    case UiState::INPUT_REWORK: drawQtyInput("LỖI SỬA ĐƯỢC", demoReworkQty); break;
    case UiState::CONFIRM_QTY: drawWorkerQtyConfirmation(); break;
    case UiState::FINISHING:
      // Giữ nguyên màn xác nhận trong lúc gửi, không chèn màn hình trung gian.
      redrawHeaderOnly = true;
      break;
    case UiState::FINISH_RETRY: drawFinishRetry(); break;
    case UiState::FINISH_SUCCESS: drawWorkerFinishSuccess(); break;
    case UiState::ERROR_STATE: drawError(); break;
    case UiState::OFFLINE: drawLoading("MẤT KẾT NỐI", "ĐANG THỬ LẠI"); break;

    case UiState::SYNC_PENDING:
      if (pendingTx.type == static_cast<uint8_t>(PendingType::START) ||
          previous == UiState::STARTING ||
          previous == UiState::OPERATION_OK ||
          previous == UiState::LOOKUP_OPERATION) {
        // START đang chờ server: giữ màn hình Operation.
        redrawHeaderOnly = true;
      } else if (pendingTx.type == static_cast<uint8_t>(PendingType::FINISH) ||
                 previous == UiState::FINISHING ||
                 previous == UiState::CONFIRM_QTY) {
        // FINISH đang chờ server: giữ màn hình xác nhận, không hiện "đang gửi/đồng bộ".
        redrawHeaderOnly = true;
      } else {
        drawPendingSync();
      }
      break;

    case UiState::TOUCH_TEST: drawTouchTestScreen(); break;
    default: drawReady(); break;
  }

  if (next != UiState::TOUCH_TEST) {
    drawTopClock(true);
    serviceNetworkIndicator(true);
  }
  (void)redrawHeaderOnly;
}

// ============================================================
// HTTP
// ============================================================
const char* friendlyApiError(int status, const char* code, const char* message) {
  const char* c = code ? code : "";
  String detail = message ? String(message) : String();
  detail.toUpperCase();
  if (strcmp(c, "STATION_OCCUPIED") == 0) return "TRẠM ĐANG ĐƯỢC SỬ DỤNG";
  if (strcmp(c, "DEVICE_NOT_BOUND") == 0 || strcmp(c, "KIOSK_AUTH_REQUIRED") == 0 ||
      strcmp(c, "KIOSK_AUTH_INVALID") == 0) return "CẦN LIÊN KẾT LẠI THIẾT BỊ";
  if (strcmp(c, "ACTIVE_SESSION_CONFLICT") == 0) return "NHÂN VIÊN ĐANG CÓ VIỆC";
  if (strcmp(c, "PO_NOT_STARTED") == 0) return "LỆNH SẢN XUẤT CHƯA BẮT ĐẦU";
  if (strcmp(c, "INVALID_OPERATION") == 0) return "CÔNG ĐOẠN KHÔNG HỢP LỆ";
  if (strcmp(c, "FINISH_TOKEN_MISSING") == 0) return "CÔNG VIỆC ĐÃ KẾT THÚC";
  if (strcmp(c, "DATABASE_BUSY") == 0) return "MÁY CHỦ ĐANG BẬN";
  if (detail.indexOf("COMPLETED") >= 0 || detail.indexOf("HOAN THANH") >= 0) return "CÔNG ĐOẠN ĐÃ HOÀN THÀNH";
  if (detail.indexOf("CANCELLED") >= 0 || detail.indexOf("CANCELED") >= 0 || detail.indexOf("HUY") >= 0) return "CÔNG ĐOẠN ĐÃ HỦY";
  if (detail.indexOf("WIP") >= 0 || detail.indexOf("DAU VAO") >= 0 || detail.indexOf("INPUT") >= 0) return "CHƯA CÓ SẢN PHẨM ĐẦU VÀO";
  if (detail.indexOf("DEPEND") >= 0 || detail.indexOf("PREVIOUS") >= 0 || detail.indexOf("CONG DOAN TRUOC") >= 0) return "CÔNG ĐOẠN TRƯỚC CHƯA XONG";
  if (status == 409) return "CHƯA THỂ BẮT ĐẦU";
  if (status == 400) return "THÔNG TIN CHƯA HỢP LỆ";
  if (status == 401) return "CẦN LIÊN KẾT LẠI THIẾT BỊ";
  if (status == 404) return "KHÔNG TÌM THẤY THÔNG TIN";
  if (status == 503 || status >= 500) return "MÁY CHỦ ĐANG BẬN";
  return "CHƯA THỰC HIỆN ĐƯỢC";
}

const char* workerFriendlyError(int status, const char* technical) {
  String detail = technical ? String(technical) : String();
  detail.toUpperCase();
  if (detail.indexOf("COMPLETED") >= 0 || detail.indexOf("HOAN THANH") >= 0) return "CÔNG ĐOẠN ĐÃ HOÀN THÀNH";
  if (detail.indexOf("CANCELLED") >= 0 || detail.indexOf("CANCELED") >= 0 || detail.indexOf("HUY") >= 0) return "CÔNG ĐOẠN ĐÃ HỦY";
  if (detail.indexOf("WIP") >= 0 || detail.indexOf("DAU VAO") >= 0 || detail.indexOf("INPUT") >= 0) return "CHƯA CÓ SẢN PHẨM ĐẦU VÀO";
  // BUG (found 2026-08-22, round 2): status==0 AND every setError() code
  // <= -20 in this file are local/business sentinels, not network codes --
  // e.g. status==0 for "QUET THE TRUOC" (OP scanned before worker card),
  // -43/-44 "THE|MA CHUA CO CACHE" (offline cache miss), -41/-42/-45..-52
  // (journal/session/sync bookkeeping). Only -1/-2/-3/-10 (our own
  // transport sentinels) and raw HTTPClient error codes (-1..-11 per the
  // ESP32 core) are real transport failures; every local sentinel below
  // that range was being caught by the old "status < 0" check too, so an
  // offline-cache-miss on OP scan (-44) STILL showed "MAT KET NOI MAY CHU"
  // even after the status==0 fix. Narrow the transport-failure band to
  // (-19, 0) exclusive of 0.
  const bool realTransportFailure = status < 0 && status > -20;
  if (detail.indexOf("WIFI") >= 0 || detail.indexOf("CONNECTION") >= 0 || detail.indexOf("TIMEOUT") >= 0 || realTransportFailure) return "MẤT KẾT NỐI MÁY CHỦ";
  // Any other status<=0 (local validation, or a local/business sentinel
  // <= -20) is already a short, clear, operator-facing Vietnamese string --
  // show it as-is instead of falling through to the generic
  // "CHUA THUC HIEN DUOC".
  if (status <= 0) return technical;
  const bool finishing = pendingTx.type == static_cast<uint8_t>(PendingType::FINISH) ||
                         uiState == UiState::FINISHING || uiState == UiState::CONFIRM_QTY;
  if (finishing) {
    if (status == 400) return "KIỂM TRA LẠI SỐ LƯỢNG";
    if (status == 401 || status == 403) return "THIẾT BỊ CẦN LIÊN KẾT LẠI";
    if (status == 404) return "KHÔNG TÌM THẤY PHIÊN LÀM VIỆC";
    if (status == 409) return "PHIÊN ĐÃ THAY ĐỔI - THỬ LẠI";
    if (status >= 500) return "MÁY CHỦ ĐANG BẬN - THỬ LẠI";
  }
  if (status == 409) return "CHƯA THỂ BẮT ĐẦU";
  if (detail.indexOf("KHONG TIM THAY") >= 0 || detail.indexOf("KHÔNG TÌM THẤY") >= 0) return "KHÔNG TÌM THẤY THÔNG TIN";
  if (detail.indexOf("CHUA") >= 0 || detail.indexOf("CHƯA") >= 0 || detail.indexOf("CONG DOAN") >= 0 ||
      detail.indexOf("CÔNG ĐOẠN") >= 0 || detail.indexOf("MAY CHU") >= 0 || detail.indexOf("MÁY CHỦ") >= 0 ||
      detail.indexOf("NHAN VIEN") >= 0 || detail.indexOf("NHÂN VIÊN") >= 0) return technical;
  return status >= 500 ? "MÁY CHỦ ĐANG BẬN" : "CHƯA THỰC HIỆN ĐƯỢC";
}

void setError(int status, const char* message) {
  returnToReadyAfterError = false;
  rt.lastHttpStatus = status;
  const char* technical = message && message[0] ? message : "UNKNOWN ERROR";
  Serial.printf("[ERROR] HTTP %d: %s\n", status, technical);
  remoteLogf("ERROR HTTP %d: %s", status, technical);
  if (suppressNetworkUiErrors) {
    safeCopy(rt.lastError, sizeof(rt.lastError), technical);
    return;
  }
  safeCopy(rt.lastError, sizeof(rt.lastError), workerFriendlyError(status, technical));
  setUi(UiState::ERROR_STATE);
}

void setApiError(int status, JsonDocument& response) {
  const char* code = response["error"] | "";
  const char* message = response["message"] | "";
  Serial.printf("[API ERROR] HTTP %d code=%s message=%s\n", status, code, message);
  setError(status, friendlyApiError(status, code, message));
}

void addAuthHeaders(HTTPClient& http) {
  http.addHeader("X-Station-ID", STATION_CODE);
  http.addHeader("X-Device-ID", DEVICE_ID);
  if (DEVICE_UUID[0]) http.addHeader("X-Device-UUID", DEVICE_UUID);
  if (rt.kioskToken[0]) http.addHeader("X-Kiosk-Token", rt.kioskToken);
}

bool decodeResponse(HTTPClient& http, int status, DynamicJsonDocument& response, JsonDocument* filter = nullptr) {
  DeserializationError err = filter
      ? deserializeJson(response, http.getStream(), DeserializationOption::Filter(*filter))
      : deserializeJson(response, http.getStream());
  if (err) {
    setError(status, err.c_str());
    return false;
  }
  return true;
}

// ROOT CAUSE (found 2026-08-22, round 4, confirmed via on-device diagnostics
// + reading the ESP32 core's HTTPClient::begin() source): this one function
// serves TWO unrelated channels -- the plain-HTTP core MES API (SERVER_BASE)
// used by every worker/OP scan, and the HTTPS OTA-agent check (OTA_AGENT_BASE,
// via baseOverride) used only by checkForOta(). Every call recorded its
// result into the SAME shared serverLinkState/rt.online used to gate the
// scan online-vs-offline-cache decision. This device was never flashed with
// an OTA CA cert (MESFLOW_OTA_CA_FILE not set), so MesHttpSession::begin()
// correctly fails closed on every single OTA HTTPS attempt (by design --
// never falls back to setInsecure()) -- but that unrelated, expected-to-fail
// background check was dragging serverLinkState down to UNREACHABLE and
// forcing every OP/worker scan into offline-cache-only lookups (error -44),
// even though the real MES API was fast and healthy the whole time.
// trackLinkState=false lets a caller (checkForOta) opt this channel out of
// the shared connectivity signal entirely.
// BUG (field report 2026-09-09, "de lau quet ma lai la bi reset"): after the
// kiosk sits idle, the AP/router can age the association out. ESP-IDF's own
// auto-reconnect (WiFi.setAutoReconnect(true), set in connectWifi()) starts
// re-associating within a second or two, and maintainConnection() nudges it
// again on its own schedule -- but a scan landing inside that window used to
// fail INSTANTLY on this one status read ("WiFi chua ket noi") and the
// operator had to scan the card again, which is exactly what "bi reset"
// looks like from the floor even when the device never rebooted.
//
// Wait, briefly and boundedly, for the reconnect already in progress instead
// of giving up on the first read. Deliberately does NOT call WiFi.begin() or
// WiFi.reconnect() here: maintainConnection() owns reconnect policy (its own
// comment explains why calling disconnect/reconnect from inside a request
// path turns a transient status into a real outage). This only observes.
//
// 3s is the budget: well under setup()'s 40s task-watchdog, and the watchdog
// is fed while waiting so a slow reconnect can never turn this into a reboot.
static bool waitForWifiBriefly(uint32_t maxWaitMs = 3000) {
  if (WiFi.status() == WL_CONNECTED) return true;
  const uint32_t started = millis();
  while (millis() - started < maxWaitMs) {
    delay(100);
    esp_task_wdt_reset();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[NETWORK] WiFi tro lai sau %lu ms cho -- khong bat nguoi dung quet lai.\n",
                    static_cast<unsigned long>(millis() - started));
      return true;
    }
  }
  return false;
}

bool httpGetJson(const char* path, DynamicJsonDocument& response, bool auth = true, JsonDocument* filter = nullptr, const char* baseOverride = nullptr, bool trackLinkState = true) {
  // Only the operator-facing MES channel waits. trackLinkState=false is, by
  // construction, the background OTA-agent channel (the only caller passing
  // it) -- it already fails closed by design on this fleet and must not stall
  // the main loop for 3s on a WiFi blip nobody is waiting on.
  if (!(trackLinkState ? waitForWifiBriefly() : (WiFi.status() == WL_CONNECTED))) {
    if (trackLinkState) recordServerResult(false,true,-1,"GET-wifi");
    setError(-1, "WiFi chua ket noi");
    return false;
  }

  char url[384];
  snprintf(url, sizeof(url), "%s%s", (baseOverride && baseOverride[0]) ? baseOverride : SERVER_BASE, path);
  Serial.printf("\n[GET] %s\n", url);

  int lastStatus = 0;
  String lastMessage = "Ket noi API that bai";

  // ESP32 doi luc tra HTTP 0 ngay sau mot request truoc do (vi du: quet OP
  // ngay sau khi tra cuu nhan vien, hoac quet the nhan vien tiep theo ngay
  // sau khi FINISH vua ghi/flash xong). Thu lai mot lan bang WiFiClient/
  // HTTPClient moi, khong lam mat worker dang duoc chon.
  // BUG CU: vong lap dung o "attempt <= 1" nen chi chay dung 1 lan --
  // retry duoi day (if (attempt < 2) delay(300)) khong bao gio co co hoi
  // chay lan thu 2, nen 1 lan HTTP 0 thoang qua la lap tuc bao "MAT KET
  // NOI MAY CHU" va bat nguoi dung phai quet lai ma.
  for (int attempt = 1; attempt <= 2; ++attempt) {
    MesHttpSession net;

    // Do lac lo: measured server TTFB alone is ~0.6-0.9s on a good link; the
    // old 1200ms connect / 2200ms request budget leaves almost no margin for
    // real WiFi/DNS/TCP latency on the device, so scans were timing out for
    // real (not just the transient "HTTP 0" case above) even on strong WiFi.
    // Match httpPostJson()'s non-quick budget -- this GET gates the live
    // scan UX just as much as those POSTs do.
    if (!net.begin(url, 4000, 6000, 15)) {
      lastStatus = -2;
      lastMessage = "http.begin that bai";
    } else {
      HTTPClient& http = net.http();
      if (auth) addAuthHeaders(http);

      int status = http.GET();
      lastStatus = status;
      rt.lastHttpStatus = status;

      if (status > 0) {
        response.clear();
        bool decoded = decodeResponse(http, status, response, filter);
        net.end();

        if (!decoded){if (trackLinkState) recordServerResult(false,false,status,"GET-decode");return false;}
        if (trackLinkState) recordServerResult(status < 500,false,status,"GET");
        Serial.printf("[GET OK] HTTP=%d json_used=%u heap=%u\n", status,
                      static_cast<unsigned>(response.memoryUsage()),
                      static_cast<unsigned>(ESP.getFreeHeap()));

        if (status < 200 || status >= 300) {
          setApiError(status, response);
          return false;
        }
        return true;
      }

      lastMessage = HTTPClient::errorToString(status);
      Serial.printf("[GET RETRY] lan %d HTTP %d: %s\n", attempt, status, lastMessage.c_str());
      net.end();
    }

    if (attempt < 2) {
      // Do not restart Wi-Fi from inside an HTTP transaction. The ESP32 Wi-Fi
      // status can briefly leave WL_CONNECTED while a socket is being closed.
      // maintainConnection() owns reconnect policy after a confirmed outage.
      delay(300);
    }
  }

  setError(lastStatus, lastMessage.c_str());
  if (trackLinkState) recordServerResult(false,false,lastStatus,"GET");
  return false;
}

bool httpPostJson(const char* path,
                  DynamicJsonDocument& request,
                  DynamicJsonDocument& response,
                  bool auth,
                  bool quick,
                  const char* baseOverride) {
  // Same reconnect-window fix as httpGetJson(), with one exception: `quick`
  // callers (heartbeat, telemetry drain) are deliberately latency-capped and
  // run unattended in the main loop -- nobody is standing there waiting for
  // them, so they must keep failing fast rather than spend 3s per attempt.
  // START/FINISH (quick=false) is an operator-facing action and gets the wait.
  const bool wifiUp = quick ? (WiFi.status() == WL_CONNECTED) : waitForWifiBriefly();
  if (!wifiUp) {
    recordServerResult(false,true,-1,"POST-wifi");
    setError(-1, "WiFi chua ket noi");
    return false;
  }

  char url[384];
  snprintf(url, sizeof(url), "%s%s", (baseOverride && baseOverride[0]) ? baseOverride : SERVER_BASE, path);

  char body[2048];
  size_t bodyLen = serializeJson(request, body, sizeof(body));
  if (bodyLen == 0 || bodyLen >= sizeof(body)) {
    setError(-3, "JSON request qua lon");
    return false;
  }

  Serial.printf("\n[POST] %s\n", url);
  Serial.println(body);

  int lastStatus = 0;
  String lastMessage = "Ket noi API that bai";

  const int maxAttempts = quick ? 1 : 2;
  const uint16_t connectTimeoutMs = quick ? 500 : 4000;
  const uint16_t requestTimeoutMs = quick ? 1200 : 6000;

  for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
    MesHttpSession net;

    if (!net.begin(url, connectTimeoutMs, requestTimeoutMs, quick ? 4 : 15)) {
      lastStatus = -2;
      lastMessage = "http.begin that bai";
    } else {
      HTTPClient& http = net.http();
      addJsonHeaders(http);
      if (auth) addAuthHeaders(http);

      String payload(body);
      int status = http.POST(payload);
      lastStatus = status;
      rt.lastHttpStatus = status;

      if (status > 0) {
        response.clear();
        bool decoded = decodeResponse(http, status, response);
        net.end();

        if (!decoded){recordServerResult(false,false,status,"POST-decode");return false;}
        recordServerResult(status < 500,false,status,"POST");
        Serial.printf("[GET OK] HTTP=%d json_used=%u heap=%u\n", status,
                      static_cast<unsigned>(response.memoryUsage()),
                      static_cast<unsigned>(ESP.getFreeHeap()));

        if (status < 200 || status >= 300) {
          setApiError(status, response);
          return false;
        }
        return true;
      }

      lastMessage = HTTPClient::errorToString(status);
      Serial.printf("[POST RETRY] lan %d HTTP %d: %s\n", attempt, status, lastMessage.c_str());
      net.end();
    }

    if (attempt < maxAttempts) {
      // Keep the network interface untouched between retries. A transient
      // WL_CONNECTED change after POST/HTTPClient::end() is not proof that the
      // access point was lost. The background connection manager will recover
      // only after the disconnect has persisted long enough.
      delay(quick ? 50 : 350);
    }
  }

  setError(lastStatus, lastMessage.c_str());
  recordServerResult(false,false,lastStatus,"POST");
  return false;
}

void urlEncode(const char* input, char* output, size_t outputSize) {
  static const char hex[] = "0123456789ABCDEF";
  size_t w = 0;
  for (size_t i = 0; input && input[i]; ++i) {
    uint8_t c = static_cast<uint8_t>(input[i]);
    bool safe = isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
    if (safe) {
      if (w + 1 >= outputSize) break;
      output[w++] = static_cast<char>(c);
    } else {
      if (w + 3 >= outputSize) break;
      output[w++] = '%';
      output[w++] = hex[c >> 4];
      output[w++] = hex[c & 0x0F];
    }
  }
  output[w] = '\0';
}

// ============================================================
// API operations
// ============================================================
bool connectWifi() {
  setUi(UiState::WIFI);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 20000) {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
    rt.online = false;
    setError(-10, "Khong ket noi duoc WiFi");
    return false;
  }

  rt.online = true;
  Serial.printf("WiFi OK. IP=%s RSSI=%d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

bool bindKiosk() {
  // Bind is a background network operation. Never leave the kiosk stuck on
  // the temporary BINDING screen after success or failure.
  const UiState previousUi = uiState;
  const bool showBindingScreen =
      previousUi == UiState::BOOT || previousUi == UiState::WIFI ||
      previousUi == UiState::BINDING;
  if (showBindingScreen) setUi(UiState::BINDING);

  DynamicJsonDocument request(768);
  DynamicJsonDocument response(1536);

  if (!DEVICE_UUID[0] || !DEVICE_SECRET[0]) {
    setError(-20, "DEVICE_IDENTITY_MISSING");
    if (showBindingScreen) setUi(UiState::READY);
    return false;
  }

  request["device_uuid"] = DEVICE_UUID;
  request["device_secret"] = DEVICE_SECRET;
  request["device_id"] = DEVICE_ID;       // metadata/legacy display only
  request["device_name"] = DEVICE_NAME;
  request["station_code"] = STATION_CODE; // server is authoritative
  request["force"] = false;
  request["hostname"] = DEVICE_ID;
  request["os"] = "ESP32-S3";
  request["firmware_version"] = FW_VERSION;
  request["firmware_build"] = FW_BUILD;
  request["hardware_model"] = HW_MODEL;
  request["ota_capable"] = true;
  request["boot_id"] = String(bootId, HEX);
  request["boot_reason"] = String((int)esp_reset_reason());
  request["browser"] = "MESFlow Embedded Kiosk";

  // Suppress the UI side effect of httpPostJson()'s own setError(): when bind
  // is retried mid-flow (rt.bound went false while the kiosk was already on
  // READY/scanning, not on the boot BINDING screen), an unsuppressed failure
  // here would flip straight to ERROR_STATE ("MAT KET NOI MAY CHU") and never
  // get reverted, since the "if (showBindingScreen) setUi(READY)" below only
  // fires for the boot-time case. Bind failures are background/retryable and
  // must never hijack whatever screen the operator is currently looking at.
  const bool previousSuppress = suppressNetworkUiErrors;
  suppressNetworkUiErrors = true;
  const bool bindPosted = httpPostJson("/api/kiosk/connect", request, response, false);
  suppressNetworkUiErrors = previousSuppress;
  if (!bindPosted) {
    Serial.println("[BIND] That bai; kiosk van vao READY va se thu lai nen.");
    remoteLogf("BIND failed HTTP=%d error=%s", rt.lastHttpStatus, rt.lastError);
    if (showBindingScreen) setUi(UiState::READY);
    return false;
  }

  const char* token = response["kiosk_token"] | "";
  if (!token[0]) {
    setError(200, "Server khong tra kiosk_token");
    Serial.println("[BIND] Khong co kiosk_token; quay ve READY.");
    setUi(UiState::READY);
    return false;
  }

  safeCopy(rt.kioskToken, sizeof(rt.kioskToken), token);
  const char* assignedStation = response["station_code"] | "";
  const char* assignedName = response["device_name"] | "";
  if (assignedStation[0]) safeCopy(STATION_CODE, sizeof(STATION_CODE), assignedStation);
  if (assignedName[0]) safeCopy(DEVICE_NAME, sizeof(DEVICE_NAME), assignedName);
  rt.bound = true;

  prefs.begin("mesflow", false);
  prefs.putString("token", rt.kioskToken);
  prefs.putString("station", STATION_CODE);
  prefs.end();

  Serial.println("Kiosk bind OK; token da luu NVS.");
  remoteLogf("BIND OK station=%s", STATION_CODE);
  if (showBindingScreen || uiState == UiState::BINDING) {
    setUi(UiState::READY);
    Serial.println("[READY] Bind thanh cong; san sang quet the.");
  }
  return true;
}

bool lookupQr(const char* qr, bool expectingWorker) {
  const uint32_t actionStartedAt = millis();
  setUi(expectingWorker ? UiState::LOOKUP_WORKER : UiState::LOOKUP_OPERATION);

  char encoded[256];
  char path[320];
  urlEncode(qr, encoded, sizeof(encoded));
  snprintf(path, sizeof(path), "/api/lookup?qr=%s", encoded);

  // Parse only fields used by the kiosk. The server lookup response can contain
  // a large active_session object and an entire active_sessions array. Filtering
  // while streaming avoids retaining unused JSON and greatly reduces heap/stack
  // pressure, especially when PSRAM is not detected.
  DynamicJsonDocument filter(1024);
  filter["type"] = true;
  filter["message"] = true;
  filter["error"] = true;
  filter["worker"]["id"] = true;
  filter["worker"]["code"] = true;
  filter["worker"]["employee_code"] = true;
  filter["worker"]["name"] = true;
  filter["operation"]["id"] = true;
  filter["operation"]["code"] = true;
  filter["operation"]["name"] = true;
  filter["operation"]["qr"] = true;
  filter["operation"]["operation_code"] = true;
  filter["operation"]["operation_name"] = true;
  filter["operation"]["operation_qr"] = true;
  const char* sessionFields[] = {
      "id", "session_group_id", "start_time", "operation_id",
      "operation_code", "operation_name", "operation_qr", "po", "part"};
  for (const char* key : sessionFields) {
    filter["active_session"][key] = true;
    filter["active_sessions"][0][key] = true;
  }

  DynamicJsonDocument response(3072);
  if (!httpGetJson(path, response, true, &filter)) {
    emitActionEvent(expectingWorker ? "EMPLOYEE_REJECTED" : "OPERATION_REJECTED",
                    "API_RESULT", "FAILED", rt.lastHttpStatus,
                    millis() - actionStartedAt, rt.lastError);
    return false;
  }

  const char* type = response["type"] | "";

  if (expectingWorker) {
    if (strcmp(type, "worker") != 0) {
      setError(200, "QR khong phai nhan vien");
      emitActionEvent("EMPLOYEE_REJECTED", "API_RESULT", "REJECTED", 200, millis() - actionStartedAt, rt.lastError);
      return false;
    }

    JsonObject worker = response["worker"];
    rt.workerId = worker["id"] | 0;
    rt.hasWorker = true;
    safeCopy(rt.workerQr, sizeof(rt.workerQr), qr);
    safeCopy(rt.workerCode, sizeof(rt.workerCode), worker["code"] | worker["employee_code"] | "");
    safeCopy(rt.workerName, sizeof(rt.workerName), worker["name"] | "Khong ro ten");
    cacheWorkerNow(qr, rt.workerCode, rt.workerName);

    JsonArray active = response["active_sessions"].as<JsonArray>();
    JsonObject activeSingle = response["active_session"].as<JsonObject>();
    rt.activeSessionId = 0;
    rt.activeGroupId[0] = '\0';
    rt.activeStartTime[0] = '\0';

    if ((!active.isNull() && active.size() > 0) || !activeSingle.isNull()) {
      JsonObject s = (!active.isNull() && active.size() > 0) ? active[0].as<JsonObject>() : activeSingle;
      rt.activeSessionId = s["id"] | 0;
      safeCopy(rt.activeGroupId, sizeof(rt.activeGroupId), s["session_group_id"] | "");
      safeCopy(rt.activeStartTime, sizeof(rt.activeStartTime), s["start_time"] | "");
      rt.operationId = s["operation_id"] | 0;
      safeCopy(rt.operationCode, sizeof(rt.operationCode), s["operation_code"] | "");
      safeCopy(rt.operationName, sizeof(rt.operationName), s["operation_name"] | "Operation dang mo");
      safeCopy(rt.operationQr, sizeof(rt.operationQr), s["operation_qr"] | "");
      rt.hasOperation = true;
      safeCopy(rt.po, sizeof(rt.po), s["po"] | "");
      safeCopy(rt.part, sizeof(rt.part), s["part"] | "");
      demoGoodQty = 0;
      demoReworkQty = 0;
      demoDefectQty = 0;
      setUi(UiState::INPUT_GOOD);
    } else {
      setUi(UiState::WORKER_OK);
    }

    emitActionEvent("EMPLOYEE_ACCEPTED", "API_RESULT", "SUCCESS", 200, millis() - actionStartedAt);
    return true;
  }

  if (strcmp(type, "operation") != 0) {
    setError(200, "QR khong phai operation");
    emitActionEvent("OPERATION_REJECTED", "API_RESULT", "REJECTED", 200, millis() - actionStartedAt, rt.lastError);
    return false;
  }

  JsonObject operation = response["operation"];
  rt.operationId = operation["id"] | 0;
  rt.hasOperation = true;
  safeCopy(rt.operationQr, sizeof(rt.operationQr), qr);
  safeCopy(rt.operationCode, sizeof(rt.operationCode), operation["code"] | operation["operation_code"] | "");
  safeCopy(rt.operationName, sizeof(rt.operationName), operation["name"] | "Khong ro operation");
  safeCopy(rt.po, sizeof(rt.po), operation["po"] | "");
  safeCopy(rt.part, sizeof(rt.part), operation["part"] | "");
  cacheOperationNow(qr, rt.operationName, rt.po, rt.part);
  setUi(UiState::OPERATION_OK);
  emitActionEvent("OPERATION_ACCEPTED", "API_RESULT", "SUCCESS", 200, millis() - actionStartedAt);
  return true;
}

void resetForNextWorker();

void releaseKioskAfterStart();
void resetForNextWorker();

bool syncPendingTransaction(bool foreground) {
  if (!hasPendingTransaction()) return true;

  if (WiFi.status() != WL_CONNECTED || !rt.bound) {
    if(!convertPendingTransactionToOfflineQueue()) setUi(static_cast<PendingType>(pendingTx.type) == PendingType::FINISH
            ? UiState::FINISH_RETRY : UiState::SYNC_PENDING);
    return false;
  }

  DynamicJsonDocument request(1792);
  DynamicJsonDocument response(2304);
  const PendingType type = static_cast<PendingType>(pendingTx.type);
  const char* path = nullptr;

  if (type == PendingType::START) {
    path = "/api/session/group/start";
    request["worker_qr"] = pendingTx.workerQr;
    JsonArray ops = request.createNestedArray("operation_qrs");
    ops.add(pendingTx.operationQr);
    request["batch_token"] = pendingTx.token;
    request["started_by"] = "ESP32_KIOSK";
    request["source"] = "ESP32_KIOSK";
    request["station_id"] = STATION_CODE;
    request["device_id"] = DEVICE_ID;
  } else if (type == PendingType::FINISH) {
    path = "/api/session/group/finish";
    request["worker_qr"] = pendingTx.workerQr;
    request["session_group_id"] = pendingTx.groupId;
    request["finish_token"] = pendingTx.token;
    request["source"] = "ESP32_KIOSK";
    request["station_id"] = STATION_CODE;
    request["device_id"] = DEVICE_ID;
    JsonArray results = request.createNestedArray("results");
    JsonObject item = results.createNestedObject();
    item["session_id"] = pendingTx.sessionId;
    item["good_qty"] = pendingTx.goodQty;
    item["defect_qty"] = pendingTx.defectQty;
    // Version-1 journal compatibility: reserved marks the new three-bucket
    // schema and createdUptime carries repairable quantity for FINISH only.
    item["rework_qty"] = pendingTx.reserved == 1 ? pendingTx.createdUptime : 0;
    item["note"] = "ESP32 durable journal";
  } else {
    setError(-31, "JOURNAL KHONG HOP LE");
    return false;
  }

  if (!httpPostJson(path, request, response, true, true)) {
    // Transport failures are uncertain: the server may have committed but the
    // ACK was lost. Keep the exact token/payload and retry idempotently.
    const int status = rt.lastHttpStatus;
    const bool transportOrRetryable = status <= 0 || status == 408 || status == 429 || status >= 500;
    if (transportOrRetryable) {
      if(!convertPendingTransactionToOfflineQueue()) setUi(type == PendingType::FINISH ? UiState::FINISH_RETRY : UiState::SYNC_PENDING);
      emitActionEvent("SYNC_FAILED", "API_RESULT", "RETRY_PENDING", status, 0, rt.lastError);
      return false;
    }

    // A normal 4xx response is a definitive business rejection. Examples:
    // PO_NOT_STARTED, INVALID_OPERATION, ACTIVE_SESSION_CONFLICT. Retrying the
    // same durable START forever would leave the kiosk stuck on DANG DONG BO.
    // Delete only this rejected intent, retain the friendly API error on screen,
    // then return to READY after a short readable delay.
    Serial.printf("[PENDING] Server tu choi nghiep vu HTTP %d - huy journal dang cho.\n", status);
    clearPendingTransaction();
    rt.batchToken[0] = '\0';
    rt.finishToken[0] = '\0';
    clearRuntimeSelection();
    emitActionEvent(type == PendingType::START ? "SESSION_START_FAILED" : "SYNC_FAILED",
                    "API_RESULT", "REJECTED", status, 0, rt.lastError);
    returnToReadyAfterError = true;
    // httpPostJson()/setApiError() already selected ERROR_STATE and populated
    // rt.lastError, so do not overwrite it with SYNC_PENDING here.
    return false;
  }

  if (type == PendingType::START) {
    safeCopy(rt.activeGroupId, sizeof(rt.activeGroupId), response["group_id"] | pendingTx.token);
    JsonArray ids = response["session_ids"].as<JsonArray>();
    rt.activeSessionId = (!ids.isNull() && ids.size()) ? (ids[0] | 0) : 0;
    safeCopy(rt.workerName, sizeof(rt.workerName), pendingTx.workerName);
    safeCopy(rt.operationName, sizeof(rt.operationName), pendingTx.operationName);
    clearPendingTransaction();
    rt.batchToken[0] = '\0';
    setUi(UiState::START_SUCCESS);
    emitActionEvent("SESSION_STARTED", "API_RESULT", "SUCCESS", 200);
    emitActionEvent("SYNC_SUCCESS", "API_RESULT", "SUCCESS", 200);
    return true;
  }

  // FINISH confirmed by server. Only now may the durable journal be deleted.
  clearPendingTransaction();
  rt.finishToken[0] = '\0';
  rt.activeSessionId = 0;
  rt.activeGroupId[0] = '\0';
  emitActionEvent("SYNC_SUCCESS", "API_RESULT", "SUCCESS", 200);
  // Do not redraw READY from inside syncPendingTransaction(). At this point the
  // function still owns two DynamicJsonDocument objects plus the completed
  // HTTP/TLS call chain on the Arduino loop-task stack. On boards without
  // PSRAM, drawing the full READY screen here can overflow/corrupt that stack
  // and cause ESP_RST_PANIC immediately after a successful FINISH.
  deferredFinishReset = true;
  Serial.printf("[FINISH] ACK OK; defer UI reset. heap=%u min_heap=%u stack_free_words=%u\n",
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMinFreeHeap()),
                static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  return true;
}

bool startSession() {
  if (!rt.hasWorker || !rt.hasOperation || !rt.workerQr[0] || !rt.operationQr[0]) {
    setError(0, "CAN QUET THE VA MA");
    return false;
  }
  // LOCAL-FIRST invariant: the START exists in the append-only LittleFS
  // journal before the worker sees success. Network only decides sync now/later.
  return offlineLookupOperationAndStart(rt.operationQr);
}

bool finishSession() {
  if (!rt.workerQr[0] || !rt.activeSessionId) {
    setError(0, "CHUA CO SESSION");
    return false;
  }
  emitActionEvent("QUANTITY_CONFIRMED", "USER_ACTION", "SUCCESS");
  const uint32_t seq=nextDeviceSequence(),epoch=currentEpoch();
  char eventId[64],localId[56];makeToken("FINISH",eventId,sizeof(eventId));
  snprintf(localId,sizeof(localId),"SERVER:%ld",(long)rt.activeSessionId);
  if(!appendOfflineEventWithIdentity(OfflineEventType::FINISH,eventId,seq,localId,
      rt.workerQr,rt.workerName,rt.operationQr,rt.operationName,demoGoodQty,
      demoDefectQty,epoch,demoReworkQty)){
    setError(-30,"KHÔNG LƯU ĐƯỢC - BÁO QUẢN LÝ");return false;
  }
  Serial.printf("[OFFLINE] local-first FINISH event=%s pending=%u\n",eventId,countPendingOfflineEvents());
  drawSimple("ĐÃ GHI NHẬN",countPendingOfflineEvents()>0?"CHỜ ĐỒNG BỘ":"","","",C_OK);
  delay(900);resetForNextWorker();return true;
}

bool canSendHeartbeat() {
  // Avoid only the exact moments where a business HTTP request is active.
  // Other screens still report state age so the backend can detect a stuck UI.
  return uiState != UiState::STARTING && uiState != UiState::FINISHING &&
         uiState != UiState::BINDING && uiState != UiState::WIFI;
}

bool sendHeartbeat() {
  if (!rt.bound || WiFi.status() != WL_CONNECTED) return false;
  if (!canSendHeartbeat()) return false;

  DynamicJsonDocument request(1536);
  DynamicJsonDocument response(768);

  request["station_code"] = STATION_CODE;
  request["device_id"] = DEVICE_ID;
  request["device_name"] = DEVICE_NAME;
  request["hostname"] = DEVICE_ID;
  request["os"] = "ESP32-S3";
  request["browser"] = "Embedded";
  request["app_version"] = APP_VERSION;
  request["firmware_version"] = FW_VERSION;
  request["firmware_build"] = FW_BUILD;
  request["hardware_model"] = HW_MODEL;
  request["ota_capable"] = true;
  request["boot_id"] = String(bootId, HEX);
  request["boot_reason"] = String((int)esp_reset_reason());

  // Day la trang thai cua kiosk dung chung, khong phai trang thai session tren server.
  // Chi gui context khi co du lieu. Mot so backend cu khong xu ly tot chuoi rong
  // cho cac cot/field tuy chon, dan den HTTP 500.
  if (sessionTraceId[0]) request["session_trace_id"] = sessionTraceId;
  if (rt.hasWorker) {
    if (rt.workerCode[0]) request["worker_code"] = rt.workerCode;
    if (rt.workerId > 0) request["worker_id"] = rt.workerId;
    if (rt.workerName[0]) request["worker_name"] = rt.workerName;
  }
  if (rt.hasOperation) {
    const char* opCode = rt.operationCode[0] ? rt.operationCode : rt.operationQr;
    if (opCode[0]) request["operation_code"] = opCode;
    if (rt.operationId > 0) request["operation_id"] = rt.operationId;
    if (rt.operationName[0]) request["operation_name"] = rt.operationName;
    if (rt.po[0]) request["po_code"] = rt.po;
  }
  request["ui_state"] = stateName(uiState);
  request["session_elapsed_seconds"] = 0;

  // Thong tin chan doan thiet bi.
  request["uptime_seconds"] = millis() / 1000UL;
  request["wifi_rssi"] = WiFi.RSSI();
  request["free_heap"] = ESP.getFreeHeap();
  request["min_free_heap"] = ESP.getMinFreeHeap();
  request["ip"] = WiFi.localIP().toString();
  request["heartbeat_fail_count"] = heartbeatFailCount;
  request["state_age_seconds"] = (millis() - stateEnteredAt) / 1000UL;
  request["last_user_action_seconds"] = lastUserActionAt ? (millis() - lastUserActionAt) / 1000UL : 0;
  request["loop_alive_age_ms"] = millis() - lastLoopAliveAt;
  request["queue_size"] = countPendingOfflineEvents() + actionQueueCount() + (hasPendingTransaction() ? 1 : 0);
  request["pending_transaction"] = hasPendingTransaction();
  request["recovery_count"] = stateRecoveryCount;
  request["last_recovery_seconds"] = lastRecoveryAt ? (millis() - lastRecoveryAt) / 1000UL : 0;
  request["last_recovery_reason"] = lastRecoveryReason;

  MesHttpSession net;
  char url[256];
  snprintf(url, sizeof(url), "%s/api/station/heartbeat", SERVER_BASE);

  if (!net.begin(url, 1500, 2500, 3)) {
    heartbeatFailCount++;
    // BUG (found 2026-08-22): heartbeat used to write rt.online directly with
    // its own private threshold (HEARTBEAT_FAILS_TO_OFFLINE=3), completely
    // independent of recordServerResult()'s serverLinkState (2-in-a-row)
    // that every other API call drives and that the OP/worker-scan online-
    // vs-offline-cache gate reads. The two trackers fought each other -- one
    // could mark the link back up while the other still held it down (or vice
    // versa) -- so the READY screen's title ("MAT KET NOI" is rt.online==
    // false) and the scan gate could disagree with reality even with an
    // empty sync queue and a perfectly good connection. Route heartbeat
    // through the same single source of truth as every other call.
    recordServerResult(false, true, -2, "HB-begin");
    Serial.println("[heartbeat] http.begin that bai");
    return false;
  }

  // Heartbeat khong phai nghiep vu quan trong: fail thi bo qua, khong chan kiosk lau.
  HTTPClient& http = net.http();
  addJsonHeaders(http);
  addAuthHeaders(http);

  char body[1536];
  size_t len = serializeJson(request, body, sizeof(body));
  if (len == 0 || len >= sizeof(body)) {
    net.end();
    Serial.println("[heartbeat] JSON qua lon");
    return false;
  }

  String payload(body);
  int status = http.POST(payload);
  String responseBody;
  if (status > 0) {
    responseBody = http.getString();
    if (responseBody.length()) {
      DeserializationError err = deserializeJson(response, responseBody);
      if (err && status >= 200 && status < 300) {
        Serial.printf("[heartbeat] JSON loi: %s\n", err.c_str());
      }
    }
  }

  // HTTP 500 phai in body de tim dung traceback/message tu backend.
  if (status < 200 || status >= 300) {
    String preview = responseBody;
    if (preview.length() > 600) preview = preview.substring(0, 600);
    preview.replace("\r", " ");
    preview.replace("\n", " ");
    Serial.printf("[heartbeat] response: %s\n", preview.length() ? preview.c_str() : "<empty>");
  }

  net.end();

  if (status >= 200 && status < 300) {
    heartbeatFailCount = 0;
    recordServerResult(true);

    if (response["server_epoch"].is<uint32_t>()) {
      syncClockFromServer(response["server_epoch"].as<uint32_t>());
      drawTopClock(true);
    }

    // Server co the tra ve config nhe trong heartbeat; field khong co thi bo qua.
    if (response["enabled"].is<bool>() && response["enabled"] == false) {
      Serial.println("[heartbeat] Server bao kiosk disabled");
    }
    if (response["config_version"].is<int>()) {
      Serial.printf("[heartbeat] config_version=%d\n", response["config_version"].as<int>());
    }

    Serial.printf("[heartbeat] OK RSSI=%d heap=%u\n",
                  WiFi.RSSI(), static_cast<unsigned>(ESP.getFreeHeap()));
    remoteLogf("HEARTBEAT OK RSSI=%d heap=%u", WiFi.RSSI(), static_cast<unsigned>(ESP.getFreeHeap()));
    return true;
  }

  if (heartbeatFailCount < 255) heartbeatFailCount++;
  recordServerResult(false, false, status, "HB");

  remoteLogf("HEARTBEAT FAIL HTTP=%d count=%u", status, static_cast<unsigned>(heartbeatFailCount));

  Serial.printf("[heartbeat] HTTP %d, fail %u/%u\n",
                status,
                static_cast<unsigned>(heartbeatFailCount),
                static_cast<unsigned>(HEARTBEAT_FAILS_TO_OFFLINE));

  if (status == 401 || status == 409) {
    rt.bound = false;
  }
  return false;
}

static void otaEvent(const char* status, const char* errorCode = "", const char* message = "") {
  if (!rt.bound || WiFi.status() != WL_CONNECTED) return;
  DynamicJsonDocument req(1024), resp(512);
  req["kiosk_id"] = DEVICE_UUID[0] ? DEVICE_UUID : DEVICE_ID;
  req["from_version"] = FW_VERSION; req["to_version"] = otaTargetVersion;
  if (otaFirmwareId[0]) req["firmware_id"] = otaFirmwareId;
  req["status"] = status; req["error_code"] = errorCode; req["message"] = message;
  req["timestamp"] = currentEpoch();
  const bool previousSuppress = suppressNetworkUiErrors;
  suppressNetworkUiErrors = true;
  httpPostJson("/api/esp-ota/event", req, resp, true, false, otaAgentBase());
  suppressNetworkUiErrors = previousSuppress;
}

static bool otaIdleSafe() {
  return WiFi.status() == WL_CONNECTED && rt.bound && rt.online && uiState == UiState::READY &&
         !rt.hasWorker && !rt.hasOperation && rt.activeSessionId <= 0 &&
         !hasPendingTransaction() && countPendingOfflineEvents() == 0 && actionQueueCount() == 0 &&
         !offlineMode && !maintenanceMode && !keypadCalibrationRequested && !keypadCalibrationInProgress;
}

static void rememberOtaBoot() {
  Preferences ota; ota.begin("mf_ota", false);
  ota.putString("from", FW_VERSION); ota.putString("to", otaTargetVersion);
  ota.putString("firmware", otaFirmwareId); ota.putBool("pending", true); ota.end();
}

static bool performOtaUpdate() {
  if (!otaIdleSafe()) { otaEvent("OTA_WAITING_IDLE", "", "OTA_AVAILABLE_WAITING_IDLE"); return false; }
  if (!String(otaDownloadUrl).startsWith("https://")) {
    otaEvent("OTA_FAILED", "OTA_HTTP_ERROR", "HTTPS firmware URL required"); return false;
  }
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (!next || otaExpectedSize == 0 || otaExpectedSize > next->size) {
    otaEvent("OTA_FAILED", "OTA_NO_SPACE", "Firmware exceeds inactive OTA partition"); return false;
  }
  otaEvent("OTA_DOWNLOAD_START");
  MesHttpSession net;
  if (!net.begin(String(otaDownloadUrl), 8000, 30000, 35, false, true)) {
    otaEvent("OTA_FAILED", "OTA_NETWORK_ERROR", "http.begin failed"); return false;
  }
  HTTPClient& http = net.http(); addAuthHeaders(http);
  int code = http.GET();
  if (code != HTTP_CODE_OK) { net.end(); otaEvent("OTA_FAILED", "OTA_HTTP_ERROR", String(code).c_str()); return false; }
  int declared = http.getSize();
  if (declared <= 0 || static_cast<size_t>(declared) != otaExpectedSize) {
    net.end(); otaEvent("OTA_FAILED", "OTA_SIZE_MISMATCH", "Content-Length mismatch"); return false;
  }
  if (!Update.begin(otaExpectedSize, U_FLASH)) {
    net.end(); otaEvent("OTA_FAILED", "OTA_FLASH_ERROR", Update.errorString()); return false;
  }
  mbedtls_sha256_context sha; mbedtls_sha256_init(&sha); mbedtls_sha256_starts(&sha, 0);
  WiFiClient* stream = http.getStreamPtr(); uint8_t buffer[4096]; size_t written = 0;
  uint32_t lastData = millis(); bool streamFailed = false;
  while (written < otaExpectedSize) {
    size_t available = stream->available();
    if (available) {
      size_t want = min(available, min(sizeof(buffer), otaExpectedSize - written));
      int got = stream->readBytes(buffer, want);
      if (got <= 0 || Update.write(buffer, got) != static_cast<size_t>(got)) { streamFailed = true; break; }
      mbedtls_sha256_update(&sha, buffer, got); written += got; lastData = millis();
    } else if (!http.connected() || millis() - lastData > 30000) { streamFailed = true; break; }
    delay(1);
  }
  uint8_t digest[32]; mbedtls_sha256_finish(&sha, digest); mbedtls_sha256_free(&sha); net.end();
  if (streamFailed || written != otaExpectedSize) { Update.abort(); otaEvent("OTA_FAILED", "OTA_SIZE_MISMATCH", "Truncated stream"); return false; }
  char actual[65]; for (uint8_t i=0;i<32;i++) sprintf(actual+i*2, "%02x", digest[i]); actual[64]='\0';
  if (strcasecmp(actual, otaExpectedSha256) != 0) { Update.abort(); otaEvent("OTA_VERIFY_FAILED", "OTA_HASH_MISMATCH", actual); return false; }
  otaEvent("OTA_DOWNLOAD_COMPLETE"); otaEvent("OTA_VERIFY_OK");
  if (!Update.end(true) || !Update.isFinished()) { otaEvent("OTA_FAILED", "OTA_FLASH_ERROR", Update.errorString()); return false; }
  rememberOtaBoot(); otaEvent("OTA_REBOOTING"); delay(300); ESP.restart(); return true;
}

static void checkForOta() {
  if (!rt.bound || WiFi.status() != WL_CONNECTED || !rt.online) return;
  // HTTPS certificate validation needs a valid clock. Do not consume the
  // long polling interval while NTP is still bootstrapping.
  if (currentEpoch() < MIN_VALID_EPOCH) {
    Serial.println("[OTA] WAITING_TIME_SYNC");
    return;
  }
  const uint32_t interval = otaAvailableWaitingIdle || !otaCheckSucceeded ? OTA_RETRY_INTERVAL_MS : OTA_CHECK_INTERVAL_MS;
  if (lastOtaCheckAt && millis() - lastOtaCheckAt < interval) return;
  lastOtaCheckAt = millis(); otaEvent("OTA_CHECK");
  Serial.printf("[OTA] CHECK agent=%s version=%s model=%s\n", otaAgentBase(), FW_VERSION, HW_MODEL);
  char path[320]; snprintf(path, sizeof(path), "/api/esp-ota/check?kiosk_id=%s&current_version=%s&hardware_model=%s",
                           DEVICE_UUID[0] ? DEVICE_UUID : DEVICE_ID, FW_VERSION, HW_MODEL);
  DynamicJsonDocument response(1536);
  const bool previousSuppress = suppressNetworkUiErrors;
  suppressNetworkUiErrors = true;
  // trackLinkState=false: the OTA agent is a separate service from the core
  // MES API and must never affect serverLinkState/rt.online (see the long
  // comment on httpGetJson()) -- otherwise a device with no OTA CA cert
  // provisioned yet can never stay "online" long enough to scan.
  const bool checked = httpGetJson(path, response, true, nullptr, otaAgentBase(), false);
  suppressNetworkUiErrors = previousSuppress;
  if (!checked) {
    otaCheckSucceeded = false;
    // Consume the retry cooldown.  Setting the timestamp in the past creates
    // a tight request loop when the Agent/network is unavailable.
    lastOtaCheckAt = millis();
    Serial.printf("[OTA] CHECK_FAILED status=%d; retry in %lus\n", rt.lastHttpStatus,
                  static_cast<unsigned long>(OTA_RETRY_INTERVAL_MS / 1000UL));
    return;
  }
  if (!(response["update_available"] | false)) { otaAvailableWaitingIdle = false; otaCheckSucceeded = true; Serial.println("[OTA] NO_UPDATE"); return; }
  otaCheckSucceeded = true;
  const char* model = response["hardware_model"] | "";
  if (strcmp(model, HW_MODEL) != 0) { otaEvent("OTA_FAILED", "OTA_WRONG_HARDWARE", model); return; }
  safeCopy(otaFirmwareId,sizeof(otaFirmwareId),response["firmware_id"] | "");
  safeCopy(otaTargetVersion,sizeof(otaTargetVersion),response["version"] | "");
  safeCopy(otaTargetBuild,sizeof(otaTargetBuild),response["build"] | "");
  safeCopy(otaDownloadUrl,sizeof(otaDownloadUrl),response["url"] | "");
  safeCopy(otaExpectedSha256,sizeof(otaExpectedSha256),response["sha256"] | "");
  otaExpectedSize = response["size"] | 0; otaAvailableWaitingIdle = true; otaEvent("OTA_AVAILABLE");
  if (!otaIdleSafe()) otaEvent("OTA_WAITING_IDLE", "", "OTA_AVAILABLE_WAITING_IDLE");
  else performOtaUpdate();
}

static void otaCheckTask(void*) {
  checkForOta();
  otaCheckTaskRunning = false;
  vTaskDelete(nullptr);
}

static void scheduleOtaCheck() {
  if (otaCheckTaskRunning || !rt.bound || WiFi.status() != WL_CONNECTED || !rt.online) return;
  const uint32_t interval = otaAvailableWaitingIdle || !otaCheckSucceeded ? OTA_RETRY_INTERVAL_MS : OTA_CHECK_INTERVAL_MS;
  if (lastOtaCheckAt && millis() - lastOtaCheckAt < interval) return;
  otaCheckTaskRunning = true;
  // HTTPS + ArduinoJson parsing uses more stack than the normal UI task.
  // Keep this isolated from the production loop and give it enough headroom
  // to prevent a stack-canary reboot while checking the Agent.
  if (xTaskCreatePinnedToCore(otaCheckTask, "mesflow-ota-check", 24576, nullptr, 1, nullptr, 0) != pdPASS) {
    otaCheckTaskRunning = false;
    otaEvent("OTA_FAILED", "OTA_NO_SPACE", "Cannot allocate OTA check task");
  }
}

static void confirmPendingOtaBoot() {
  Preferences ota; ota.begin("mf_ota", false); bool pending=ota.getBool("pending",false);
  String target=ota.getString("to",""); String firmware=ota.getString("firmware","");
  if (!pending) { ota.end(); return; }
  if (target != FW_VERSION) {
    safeCopy(otaTargetVersion,sizeof(otaTargetVersion),target.c_str()); safeCopy(otaFirmwareId,sizeof(otaFirmwareId),firmware.c_str());
    ota.putBool("pending",false); ota.end(); otaEvent("OTA_ROLLBACK","OTA_BOOT_FAILED","Bootloader returned to previous image"); return;
  }
  safeCopy(otaTargetVersion,sizeof(otaTargetVersion),target.c_str()); safeCopy(otaFirmwareId,sizeof(otaFirmwareId),firmware.c_str());
  otaEvent("OTA_BOOT_NEW_VERSION");
  esp_ota_img_states_t state; const esp_partition_t* running=esp_ota_get_running_partition();
  if (running && esp_ota_get_state_partition(running,&state)==ESP_OK && state==ESP_OTA_IMG_PENDING_VERIFY) {
    if (esp_ota_mark_app_valid_cancel_rollback()!=ESP_OK) { otaEvent("OTA_FAILED","OTA_BOOT_FAILED","mark valid failed"); ota.end(); return; }
  }
  ota.putBool("pending",false); ota.end(); otaEvent("OTA_HEALTHCHECK_OK");
}

// ============================================================
// Demo flow and Serial controls
// ============================================================
void printConfig() {
  Serial.println("\n---------------- DEMO CONFIG ----------------");
  Serial.printf("worker = %s\n", demoWorkerQr[0] ? demoWorkerQr : "<CHUA CAU HINH>");
  Serial.printf("op     = %s\n", demoOperationQr[0] ? demoOperationQr : "<CHUA CAU HINH>");
  Serial.printf("good   = %d\n", demoGoodQty);
  Serial.printf("defect = %d\n", demoDefectQty);
  Serial.println("---------------------------------------------\n");
}

void saveDemoConfig() {
  prefs.begin("mesflow", false);
  prefs.putString("workerQr", demoWorkerQr);
  prefs.putString("opQr", demoOperationQr);
  prefs.putInt("goodQty", demoGoodQty);
  prefs.putInt("defectQty", demoDefectQty);
  prefs.end();
  Serial.println("[CONFIG] Da luu vao NVS.");
}

void printHelp() {
  Serial.println("\n================ MESFLOW DEMO v0.7 =================");
  Serial.println("Dan QR that truc tiep vao Serial Monitor (Newline):");
  Serial.println("  WF|EMP|...       -> lookup worker va cap nhat man hinh ngay");
  Serial.println("  WF|OP|...        -> lookup operation va start session ngay");
  Serial.println("Hoac dung lenh:");
  Serial.println("  worker=<QR that>");
  Serial.println("  op=<QR that>");
  Serial.println("  opid=<operation id>   (tu tao WF|OP|<id>)");
  Serial.println("  good=12");
  Serial.println("  defect=1");
  Serial.println("  ok / finish      -> xac nhan ket thuc session");
  Serial.println("  show              xem cau hinh");
  Serial.println("  testw             test lookup worker");
  Serial.println("  testo             test lookup operation");
  Serial.println("  auto              chay day du cac man hinh + API that");
  Serial.println("  bind / heartbeat / reset / setup / help");
  Serial.println("=====================================================\n");
}

bool validateDemoConfig() {
  if (!demoWorkerQr[0]) {
    setError(0, "CHUA NHAP QR NHAN VIEN");
    Serial.println("Nhap: worker=WF|EMP|...");
    return false;
  }
  if (!demoOperationQr[0]) {
    setError(0, "CHUA NHAP QR OPERATION");
    Serial.println("Nhap: op=WF|OP|...  hoac opid=<operation id>");
    return false;
  }
  if (strncmp(demoWorkerQr, "WF|EMP|", 7) != 0) {
    setError(0, "QR WORKER SAI DINH DANG");
    return false;
  }
  if (strncmp(demoOperationQr, "WF|OP|", 6) != 0) {
    setError(0, "QR OP SAI DINH DANG");
    return false;
  }
  return true;
}

void autoFlow() {
  Serial.println("[AUTO] BAT DAU CHU KY DEMO DAY DU");
  if (!validateDemoConfig()) return;

  if (WiFi.status() != WL_CONNECTED && !connectWifi()) return;
  if (!rt.bound && !bindKiosk()) return;

  setUi(UiState::SIM_SCAN_WORKER);
  delay(AUTO_STEP_DELAY_MS);
  if (!lookupQr(demoWorkerQr, true)) return;
  delay(AUTO_STEP_DELAY_MS);

  if (rt.activeSessionId) {
    Serial.println("[AUTO] PHAT HIEN SESSION CU - DANG KET THUC");
    if (!finishSession()) return;
    delay(AUTO_STEP_DELAY_MS);
    clearRuntimeSelection();
    if (!lookupQr(demoWorkerQr, true)) return;
    if (rt.activeSessionId) {
      setError(409, "SESSION CU CHUA DONG");
      return;
    }
    delay(AUTO_STEP_DELAY_MS);
  }

  setUi(UiState::SIM_SCAN_OPERATION);
  delay(AUTO_STEP_DELAY_MS);
  if (!lookupQr(demoOperationQr, false)) return;
  delay(AUTO_STEP_DELAY_MS);

  if (!startSession()) {
    if (rt.lastHttpStatus == 409) {
      Serial.println("[AUTO] START BI 409 - LOOKUP WORKER DE KIEM TRA SESSION");
      delay(AUTO_STEP_DELAY_MS);
      if (lookupQr(demoWorkerQr, true) && rt.activeSessionId) {
        delay(AUTO_STEP_DELAY_MS);
        finishSession();
      }
    }
    return;
  }

  Serial.printf("[AUTO] DANG LAM %lu ms\n", (unsigned long)AUTO_WORK_TIME_MS);
  delay(AUTO_WORK_TIME_MS);

  setUi(UiState::SIM_SCAN_WORKER);
  delay(AUTO_STEP_DELAY_MS);
  if (!lookupQr(demoWorkerQr, true)) return;
  if (!rt.activeSessionId) {
    setError(404, "KHONG THAY SESSION MO");
    return;
  }
  delay(AUTO_STEP_DELAY_MS);

  setUi(UiState::INPUT_GOOD);
  delay(SIM_KEY_DELAY_MS * 2);
  setUi(UiState::INPUT_DEFECT);
  delay(SIM_KEY_DELAY_MS * 2);
  setUi(UiState::ASK_REWORK);
  delay(SIM_KEY_DELAY_MS * 2);
  setUi(UiState::INPUT_REWORK);
  delay(SIM_KEY_DELAY_MS * 2);
  setUi(UiState::CONFIRM_QTY);
  delay(AUTO_STEP_DELAY_MS);
  if (!finishSession()) return;
  Serial.println("[AUTO] CHU KY DEMO HOAN TAT");
  // Khong reset ngay tai day de nguoi dung con thay man hinh thanh cong.
  // loop() se tu dong dua kiosk ve man hinh quet the sau FINISH_SUCCESS_HOLD_MS.
}


void releaseKioskAfterStart() {
  emitActionEvent("RETURN_TO_READY", "UI_STATE", "SUCCESS");
  endSessionTrace();
  // Session remains open on the MESFlow server. This shared kiosk only
  // releases its local worker/operation selection so another worker can scan.
  clearRuntimeSelection();
  rt.lastHttpStatus = 0;
  rt.lastError[0] = '\0';
  Serial.println("[KIOSK] Session da bat dau - tra kiosk ve man hinh quet the.");
  setUi(UiState::READY);
}

void resetForNextWorker() {
  emitActionEvent("RETURN_TO_READY", "UI_STATE", "SUCCESS");
  endSessionTrace();
  // Chi xoa du lieu cua chu ky vua ket thuc.
  // Giu WiFi, kiosk token, station binding va cau hinh demo.
  clearRuntimeSelection();
  rt.lastHttpStatus = 0;
  rt.lastError[0] = '\0';
  Serial.println("[KIOSK] Session da ket thuc - san sang cho nhan vien tiep theo.");
  setUi(UiState::READY);
}


// ============================================================
// Console command framework v3.2.0
// Administrative commands are handled before QR/session blocking so that
// recovery commands remain available even while a transaction is pending.
// ============================================================
static void consolePrintStatus() {
  Serial.println("\n================ MESFLOW STATUS ================");
  Serial.printf("Firmware : %s\n", APP_VERSION);
  Serial.printf("Device   : %s (%s)\n", DEVICE_ID, DEVICE_NAME);
  Serial.printf("Station  : %s\n", STATION_CODE);
  Serial.printf("Server   : %s\n", SERVER_BASE);
  Serial.printf("WiFi     : %s\n", WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED");
  Serial.printf("SSID     : %s\n", WIFI_SSID[0] ? WIFI_SSID : "<NOT SET>");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("IP       : %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("RSSI     : %d dBm\n", WiFi.RSSI());
  }
  Serial.printf("Bound    : %s\n", rt.bound ? "YES" : "NO");
  Serial.printf("Token    : %s\n", rt.kioskToken[0] ? "STORED" : "EMPTY");
  Serial.printf("Pending  : %s\n", hasPendingTransaction() ? "YES" : "NO");
  Serial.printf("OfflineQ : %u\n", countPendingOfflineEvents());
  Serial.printf("Cache    : workers=%u operations=%u sessions=%u\n",
                workerCacheCount, operationCacheCount, offlineSessionCount);
  Serial.printf("LittleFS : %s\n", fsReady ? "READY" : "NOT READY");
  Serial.printf("Keypad   : %s", !keypadAvailable ? "NOT FOUND" :
                (keypadMappingReady ? "READY" : "NEEDS CALIBRATION"));
  if (keypadAvailable) Serial.printf(" @ 0x%02X", keypadAddress);
  Serial.println();
  Serial.printf("Heap     : %u bytes free; largest=%u\n",
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
  Serial.printf("PSRAM    : %s total=%u free=%u buffers=%s\n",
                psramFound() ? "YES" : "NO",
                static_cast<unsigned>(ESP.getPsramSize()),
                static_cast<unsigned>(ESP.getFreePsram()),
                offlineBuffersReady ? "READY" : "DISABLED");
  Serial.println("=================================================\n");
}

static void consolePrintHelp() {
  Serial.println("\n================ CONSOLE COMMANDS ================");
  Serial.println("help | ?                 : danh sach lenh");
  Serial.println("status                    : trang thai day du");
  Serial.println("config                    : xem cau hinh (khong hien password/token)");
  Serial.println("clear-token               : xoa kiosk token, giu WiFi/server");
  Serial.println("clear-config              : xoa WiFi/server/device/token, giu du lieu offline");
  Serial.println("clear-cache               : xoa worker/operation cache, giu queue/session");
  Serial.println("clear-offline             : xoa queue + offline sessions (CAN THAN)");
  Serial.println("factory-reset             : xoa config/runtime + LittleFS; GIU identity");
  Serial.println("keypad-calibrate          : hieu chinh lai keypad ngay, khong reboot");
  Serial.println("restart | reboot          : khoi dong lai ESP32");
  Serial.println("bind                      : bind kiosk lai");
  Serial.println("heartbeat                 : gui heartbeat ngay");
  Serial.println("retry | dongbo            : thu dong bo pending transaction");
  Serial.println("reset | r                 : ve man hinh READY, khong xoa setting");
  Serial.println("==================================================\n");
}

static void clearNamespace(const char* name) {
  Preferences temp;
  temp.begin(name, false);
  temp.clear();
  temp.end();
}

static void clearKeypadCalibration() {
  clearNamespace(KEYPAD_NVS_NAMESPACE);
  keypadMappingReady = false;
  Serial.println("[KEYPAD] Da xoa mapping khoi NVS.");
}

static void forceSetupOnNextBoot() {
  // An empty key must be stored. Merely removing it would make loadDeviceConfig()
  // fall back to the firmware's development SSID.
  Preferences temp;
  temp.begin("mesflow_cfg", false);
  temp.putString("wifi_ssid", "");
  temp.putString("wifi_pass", "");
  temp.end();
}

static void consoleClearWifi() {
  Serial.println("[CONSOLE] Dang xoa WiFi...");
  Preferences temp;
  temp.begin("mesflow_cfg", false);
  temp.putString("wifi_ssid", "");
  temp.putString("wifi_pass", "");
  temp.end();
  WIFI_SSID[0] = '\0';
  WIFI_PASSWORD[0] = '\0';
  Serial.println("[CONSOLE] Da xoa WiFi. Reboot vao Setup Portal.");
  delay(700);
  ESP.restart();
}

static void consoleClearToken() {
  Preferences temp;
  temp.begin("mesflow", false);
  temp.remove("token");
  temp.end();
  rt.kioskToken[0] = '\0';
  rt.bound = false;
  Serial.println("[CONSOLE] Da xoa kiosk token. Dung 'bind' hoac reboot de bind lai.");
}

static void consoleClearConfig() {
  Serial.println("[CONSOLE] Dang xoa device config va token; giu LittleFS/offline queue...");
  clearNamespace("mesflow_cfg");
  clearNamespace("mesflow");
  forceSetupOnNextBoot();
  Serial.println("[CONSOLE] Hoan tat. Reboot vao Setup Portal.");
  delay(700);
  ESP.restart();
}

static void consoleClearCache() {
  if (!fsReady) {
    Serial.println("[CONSOLE] LittleFS chua san sang.");
    return;
  }
  bool a = LittleFS.remove(WORKER_CACHE_FILE);
  bool b = LittleFS.remove(OP_CACHE_FILE);
  workerCacheCount = 0;
  operationCacheCount = 0;
  Serial.printf("[CONSOLE] Cache da xoa (workers=%s, operations=%s).\n",
                a ? "OK" : "EMPTY", b ? "OK" : "EMPTY");
}

static void consoleClearOffline() {
  if (!fsReady) {
    Serial.println("[CONSOLE] LittleFS chua san sang.");
    return;
  }
  LittleFS.remove(SESSION_FILE);
  LittleFS.remove(EVENT_LOG_FILE);
  offlineSessionCount = 0;
  clearPendingTransaction();
  Serial.println("[CONSOLE] DA XOA queue va offline sessions. Du lieu chua sync khong the phuc hoi.");
}

static void consoleFactoryReset() {
  Serial.println("[CONSOLE] FACTORY RESET: xoa config/runtime/LittleFS; GIU mf_identity...");
  clearNamespace("mesflow_cfg");
  clearNamespace("mesflow");
  if (!LittleFS.begin(true)) {
    Serial.println("[CONSOLE] Khong mount duoc LittleFS de format.");
  } else if (!LittleFS.format()) {
    Serial.println("[CONSOLE] LittleFS format FAIL.");
  } else {
    Serial.println("[CONSOLE] LittleFS format OK.");
  }
  forceSetupOnNextBoot();
  Serial.println("[CONSOLE] Factory reset OK. Reboot vao Setup Portal...");
  Serial.flush();
  delay(1000);
  ESP.restart();
}

static bool handleConsoleCommand(String line) {
  line.trim();
  String cmd = line;
  cmd.toLowerCase();

  if (cmd == "help" || cmd == "?") { consolePrintHelp(); return true; }
  if (cmd == "status") { consolePrintStatus(); return true; }
  if (cmd == "config" || cmd == "show-config") {
    Serial.printf("[CONFIG] SSID=%s server=%s device=%s name=%s station=%s identity=%s token=%s\n",
                  WIFI_SSID[0] ? WIFI_SSID : "<NOT SET>", SERVER_BASE[0] ? SERVER_BASE : "<NOT SET>",
                  DEVICE_ID, DEVICE_NAME, STATION_CODE, DEVICE_UUID,
                  rt.kioskToken[0] ? "STORED" : "EMPTY");
    return true;
  }
  if (cmd == "restart" || cmd == "reboot") {
    Serial.println("[CONSOLE] Reboot..."); Serial.flush(); delay(300); ESP.restart(); return true;
  }
  if (cmd == "clear-token" || cmd == "token-reset") { consoleClearToken(); return true; }
  if (cmd == "clear-config" || cmd == "reset-config") { consoleClearConfig(); return true; }
  if (cmd == "clear-cache") { consoleClearCache(); return true; }
  if (cmd == "clear-offline") { consoleClearOffline(); return true; }
  if (cmd == "factory-reset" || cmd == "factory-reset all") { consoleFactoryReset(); return true; }
  if (cmd == "keypad-calibrate" || cmd == "calibrate-keypad") {
    if (requestRuntimeKeypadCalibration("SERIAL")) {
      Serial.println("[KEYPAD] Da nhan lenh; bat dau hieu chinh ngay, khong reboot.");
    } else {
      Serial.println("[KEYPAD] Khong the hieu chinh luc nay. Dua kiosk ve READY va thu lai.");
    }
    return true;
  }
  if (cmd == "touch-test" || cmd == "touch") { setUi(UiState::TOUCH_TEST); Serial.println("Da mo test cam ung."); return true; }
  if (cmd == "bind") { bindKiosk(); return true; }
  if (cmd == "heartbeat") { sendHeartbeat(); return true; }
  if (cmd == "retry" || cmd == "dongbo") {
    if (hasPendingTransaction()) syncPendingTransaction(true);
    else Serial.println("[CONSOLE] Khong co pending transaction.");
    return true;
  }
  if (cmd == "reset" || cmd == "r") {
    clearRuntimeSelection();
    demoGoodQty = 0;
    demoReworkQty = 0;
    demoDefectQty = 0;
    rt.lastError[0] = '\0';
    setUi(UiState::READY);
    return true;
  }
  return false;
}

void handleSerialLine(String line) {
  line.trim();
  if (!line.length()) return;
  lastInputAtMs = millis();
  inputEventCount++;
  safeCopy(lastInputPreview, sizeof(lastInputPreview), line.c_str());

  // Console administration always has priority over QR/session state.
  if (handleConsoleCommand(line)) return;

  // Never trap the operator on an error page. Clear the transient error first;
  // an employee scan may immediately continue into the normal lookup flow.
  if (uiState == UiState::ERROR_STATE) {
    const bool employeeScan = line.startsWith("WF|EMP|");
    recoverUiFromStuck("INPUT_DISMISSED_ERROR", true);
    if (!employeeScan) return;
  }

  String pendingCmd = line;
  pendingCmd.toLowerCase();
  if (hasPendingTransaction()) {
    if (pendingCmd == "retry" || pendingCmd == "dongbo") {
      syncPendingTransaction(true);
    } else if (pendingCmd == "status") {
      Serial.printf("[JOURNAL] pending type=%u token=%s session=%ld DAT=%ld LOI=%ld\n",
                    pendingTx.type, pendingTx.token,
                    static_cast<long>(pendingTx.sessionId),
                    static_cast<long>(pendingTx.goodQty),
                    static_cast<long>(pendingTx.defectQty));
      setUi(UiState::SYNC_PENDING);
    } else {
      Serial.println("[KIOSK] Dang co giao dich chua dong bo; khong nhan thao tac moi.");
      emitActionEvent("INPUT_REJECTED", "USER_ERROR", "REJECTED", 0, 0,
                      "Dang co giao dich cho dong bo", "PENDING_TRANSACTION", "UNKNOWN");
      setUi(UiState::SYNC_PENDING);
    }
    return;
  }

  if (line.startsWith("WF|EMP|")) {
    beginSessionTrace();
    emitActionEvent("EMPLOYEE_SCANNED", "USER_ACTION", "RECEIVED");
    Serial.printf("[SCAN EMP] %s\n", line.c_str());
    if (offlineMode || WiFi.status() != WL_CONNECTED || serverLinkState==ServerLinkState::UNREACHABLE || serverLinkState==ServerLinkState::WIFI_DOWN) {
      offlineMode = true;
      if (!offlineLookupWorker(line.c_str())) return;
    } else {
      if (!rt.bound && !bindKiosk()) return;
      clearRuntimeSelection();
      if (!lookupQr(line.c_str(), true)) return;
    }

    // BUG (found 2026-08-22): the online lookupQr() above trusts ONLY the
    // server's view of active_session. But START is LOCAL-FIRST -- it's
    // written to the local offline journal instantly and only synced to the
    // server after a backoff delay (starts at 5s). If this same worker's
    // card is re-scanned before that sync lands (very plausible -- scan
    // worker, scan OP, then immediately re-scan the same worker card to
    // enter quantity), the server still doesn't know about the session yet,
    // so rt.activeSessionId stays 0 and the operator gets bounced back to
    // "scan OP" as if no session exists, even though one was already
    // durably created. Fall back to the local session record, same as the
    // offline path already does, before deciding there's really no session.
    if (rt.activeSessionId == 0) {
      int si = findOfflineSession(rt.workerQr);
      if (si >= 0) {
        OfflineSession& s = offlineSessions[si];
        rt.activeSessionId = -1;
        safeCopy(rt.activeGroupId, sizeof(rt.activeGroupId), s.localSessionId);
        safeCopy(rt.operationQr, sizeof(rt.operationQr), s.operationQr);
        safeCopy(rt.operationName, sizeof(rt.operationName), s.operationName);
        rt.hasOperation = true;
        Serial.println("[KIOSK] Session cuc bo chua sync nhung van con hieu luc.");
      }
    }

    if (rt.activeSessionId != 0) {
      emitActionEvent("FINISH_REQUESTED", "USER_ACTION", "PENDING");
      demoGoodQty = 0;
      demoReworkQty = 0;
      demoDefectQty = 0;
      Serial.println("[KIOSK] Co session mo -> nhap so dat.");
      setUi(UiState::INPUT_GOOD);
    } else {
      Serial.println("[KIOSK] Cho quet operation.");
    }
    return;
  }

  if (line.startsWith("WF|OP|")) {
    emitActionEvent("OPERATION_SCANNED", "USER_ACTION", "RECEIVED");
    Serial.printf("[SCAN OP] %s\n", line.c_str());
    if (!rt.hasWorker || !rt.workerQr[0]) {
      setError(0, "QUET THE TRUOC");
      emitActionEvent("OPERATION_REJECTED", "USER_ERROR", "REJECTED", 0, 0,
                      "Quet operation khi chua quet the nhan vien",
                      "WORKER_REQUIRED", "OPERATION_QR");
      return;
    }
    if (rt.activeSessionId != 0) {
      setError(0, "DANG CO SESSION");
      emitActionEvent("OPERATION_REJECTED", "USER_ERROR", "REJECTED", 0, 0,
                      "Quet operation trong khi nhan vien dang co session",
                      "ACTIVE_SESSION_EXISTS", "OPERATION_QR");
      return;
    }

    if (offlineMode || WiFi.status() != WL_CONNECTED || serverLinkState==ServerLinkState::UNREACHABLE || serverLinkState==ServerLinkState::WIFI_DOWN) {
      offlineMode = true;
      offlineLookupOperationAndStart(line.c_str());
      return;
    }

    const int selectedWorkerId = rt.workerId;
    char selectedWorkerQr[sizeof(rt.workerQr)];
    char selectedWorkerCode[sizeof(rt.workerCode)];
    char selectedWorkerName[sizeof(rt.workerName)];
    safeCopy(selectedWorkerQr, sizeof(selectedWorkerQr), rt.workerQr);
    safeCopy(selectedWorkerCode, sizeof(selectedWorkerCode), rt.workerCode);
    safeCopy(selectedWorkerName, sizeof(selectedWorkerName), rt.workerName);

    if (!lookupQr(line.c_str(), false)) {
      rt.workerId = selectedWorkerId;
      rt.hasWorker = true;
      safeCopy(rt.workerQr, sizeof(rt.workerQr), selectedWorkerQr);
      safeCopy(rt.workerCode, sizeof(rt.workerCode), selectedWorkerCode);
      safeCopy(rt.workerName, sizeof(rt.workerName), selectedWorkerName);
      return;
    }
    // Chi giu rat ngan de man hinh xac nhan ma, sau do START ngay.
    // Man hinh START_SUCCESS se giu tinh 10 giay roi tu ve QUET THE.
    delay(OP_REVIEW_HOLD_MS);
    if (!startSession()) {
      rt.workerId = selectedWorkerId;
      rt.hasWorker = true;
      safeCopy(rt.workerQr, sizeof(rt.workerQr), selectedWorkerQr);
      safeCopy(rt.workerCode, sizeof(rt.workerCode), selectedWorkerCode);
      safeCopy(rt.workerName, sizeof(rt.workerName), selectedWorkerName);
      return;
    }
    return;
  }

  // Nhap so truc tiep, khong can good= / defect=.
  bool allDigits = true;
  for (size_t i = 0; i < line.length(); ++i) {
    if (!isDigit(line[i])) { allDigits = false; break; }
  }

  if (allDigits) {
    long number = line.toInt();
    if (number < 0) number = 0;
    if (number > 999999) {
      number = 999999;
      emitActionEvent("QUANTITY_ADJUSTED", "USER_ERROR", "CLAMPED", 0, 0,
                      "So luong vuot gioi han, da gioi han ve 999999",
                      "QUANTITY_TOO_LARGE", "NUMERIC_INPUT");
    }

    if (uiState == UiState::INPUT_GOOD) {
      demoGoodQty = static_cast<int>(number);
      Serial.printf("[QTY] Dat = %d\n", demoGoodQty);
      setUi(UiState::INPUT_DEFECT);
      return;
    }
    if (uiState == UiState::INPUT_DEFECT) {
      demoDefectQty = static_cast<int>(number);
      Serial.printf("[QTY] Tong loi = %d\n", demoDefectQty);
      if (demoDefectQty == 0) {
        demoReworkQty = 0;
        setUi(UiState::CONFIRM_QTY);
      } else {
        setUi(UiState::ASK_REWORK);
      }
      return;
    }
    if (uiState == UiState::ASK_REWORK) {
      if (line == "1") {
        demoReworkQty = 0;
        setUi(UiState::CONFIRM_QTY);
      } else if (line == "2") {
        demoReworkQty = 0;
        setUi(UiState::INPUT_REWORK);
      } else {
        Serial.println("[QTY] ASK_REWORK chi nhan 1 hoac 2.");
      }
      return;
    }
    if (uiState == UiState::INPUT_REWORK) {
      demoReworkQty = static_cast<int>(number);
      Serial.printf("[QTY] Loi sua duoc = %d\n", demoReworkQty);
      if (demoReworkQty <= 0 || demoReworkQty > demoDefectQty) {
        Serial.printf("[QTY] REJECT rework=%d defect=%d\n", demoReworkQty, demoDefectQty);
        drawSimple("SỐ LƯỢNG CHƯA ĐÚNG", "LỖI SỬA ĐƯỢC",
                   demoReworkQty <= 0 ? "NHẬP SỐ LỚN HƠN 0" : "KHÔNG LỚN HƠN LỖI TỔNG",
                   "THỬ LẠI", C_ERR);
        delay(1800);
        setUi(UiState::INPUT_REWORK);
        return;
      }
      setUi(UiState::CONFIRM_QTY);
      return;
    }
    if (uiState == UiState::CONFIRM_QTY) {
      if (line == "1") {
        if (offlineMode || rt.activeSessionId < 0) offlineFinishSession();
        else finishSession();
        return;
      }
      if (line == "2") {
        setUi(demoReworkQty > 0 ? UiState::INPUT_REWORK : UiState::INPUT_DEFECT);
        return;
      }
      setError(0, "CHON 1 HOAC 2");
      emitActionEvent("QUANTITY_CONFIRM_REJECTED", "USER_ERROR", "REJECTED", 0, 0,
                      "Lua chon xac nhan khong hop le",
                      "CONFIRM_OPTION_INVALID", "NUMERIC_INPUT");
      return;
    }

    if (uiState == UiState::FINISH_RETRY) {
      if (line == "1") syncPendingTransaction(true);
      else if (line == "2") setUi(UiState::CONFIRM_QTY);
      return;
    }

    emitActionEvent("INPUT_REJECTED", "USER_ERROR", "REJECTED", 0, 0,
                    "Nhap so khi kiosk khong o man hinh nhap so luong",
                    "QUANTITY_NOT_EXPECTED", "NUMERIC_INPUT");
    setError(0, "CHUA DEN BUOC NHAP SO");
    return;
  }

  String cmd = line;
  cmd.toLowerCase();
  const char* inputType = line.startsWith("WF|") ? "QR_UNKNOWN" : "UNKNOWN";
  emitActionEvent("INPUT_REJECTED", "USER_ERROR", "REJECTED", 0, 0,
                  "Du lieu quet/nhap khong dung dinh dang hoac khong dung buoc",
                  line.startsWith("WF|") ? "QR_FORMAT_UNSUPPORTED" : "INPUT_UNRECOGNIZED",
                  inputType);
  setError(0, line.startsWith("WF|") ? "QR KHONG HO TRO" : "DU LIEU KHONG HOP LE");
  Serial.printf("[BO QUA] %s\n", line.c_str());
}

void readSerialCommands() {
  static String line;
  static uint32_t lastCharAt = 0;

  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());
    lastCharAt = millis();

    // Chap nhan ca New Line, Carriage Return va Both NL & CR.
    if (c == '\r' || c == '\n') {
      if (line.length() > 0) {
        Serial.printf("[SERIAL RX] %s\n", line.c_str());
        lastInputWasVirtual = false;
        lastInputWasKeypad = false;
        handleSerialLine(line);
        line = "";
      }
      continue;
    }

    if (line.length() < 512) line += c;
  }

  // Ke ca Serial Monitor dang de "No line ending", van xu ly sau 350 ms.
  if (line.length() > 0 && millis() - lastCharAt >= 350) {
    Serial.printf("[SERIAL RX/TIMEOUT] %s\n", line.c_str());
    lastInputWasVirtual = false;
    lastInputWasKeypad = false;
    handleSerialLine(line);
    line = "";
  }
}

void dispatchScannerFrame(const char* reason) {
  if (scannerFrameLength == 0) return;

  scannerFrameCount++;
  Serial.println();
  Serial.printf("========== SCANNER FRAME %lu / %s ==========\n",
                static_cast<unsigned long>(scannerFrameCount), reason);
  Serial.printf("Do dai: %u byte\n", static_cast<unsigned>(scannerFrameLength));

  Serial.print("HEX  : ");
  for (size_t i = 0; i < scannerFrameLength; ++i) {
    if (scannerFrame[i] < 0x10) Serial.print('0');
    Serial.print(scannerFrame[i], HEX);
    Serial.print(' ');
  }
  Serial.println();

  char decoded[SCANNER_FRAME_MAX + 1] = {0};
  size_t decodedLength = 0;

  Serial.print("ASCII: ");
  for (size_t i = 0; i < scannerFrameLength; ++i) {
    const uint8_t value = scannerFrame[i];
    if (value >= 32 && value <= 126) {
      Serial.write(value);
      if (decodedLength < SCANNER_FRAME_MAX) {
        decoded[decodedLength++] = static_cast<char>(value);
      }
    } else if (value == '\r') {
      Serial.print("<CR>");
    } else if (value == '\n') {
      Serial.print("<LF>");
    } else if (value == '\t') {
      Serial.print("<TAB>");
    } else {
      Serial.print('.');
    }
  }
  Serial.println();
  decoded[decodedLength] = '\0';

  // Remove surrounding spaces in the same way as the virtual scanner input.
  String scannerText(decoded);
  scannerText.trim();
  Serial.printf("[SCANNER STRING] len=%u data=%s\n",
                static_cast<unsigned>(scannerText.length()),
                scannerText.c_str());

  if (scannerText.startsWith("WF|EMP|") || scannerText.startsWith("WF|OP|")) {
    lastInputWasVirtual = false;
    lastInputWasKeypad = false;
    handleSerialLine(scannerText);
  } else {
    Serial.println("[SCANNER DROP] Frame khong co chuoi QR MESFlow hop le; khong gui vao MES.");
  }

  scannerFrameLength = 0;
}

void readScannerCommands() {
  while (ScannerSerial.available() > 0) {
    const int raw = ScannerSerial.read();
    if (raw < 0) continue;

    const uint8_t value = static_cast<uint8_t>(raw);
    scannerByteCount++;
    scannerLastByteAt = millis();

    Serial.printf("[SCANNER BYTE %lu] HEX=0x%02X",
                  static_cast<unsigned long>(scannerByteCount), value);
    if (value >= 32 && value <= 126) {
      Serial.printf(" ASCII='%c'", static_cast<char>(value));
    } else if (value == '\r') {
      Serial.print(" <CR>");
    } else if (value == '\n') {
      Serial.print(" <LF>");
    }
    Serial.println();

    if (scannerFrameLength < SCANNER_FRAME_MAX) {
      scannerFrame[scannerFrameLength++] = value;
    } else {
      Serial.println("[SCANNER WARNING] Frame day; in frame hien tai va bat dau lai.");
      dispatchScannerFrame("BUFFER_FULL");
      scannerFrame[scannerFrameLength++] = value;
    }
  }

  // A quiet gap of 50 ms marks the end of one scanner frame.
  if (scannerFrameLength > 0 &&
      millis() - scannerLastByteAt >= SCANNER_FRAME_TIMEOUT_MS) {
    dispatchScannerFrame("TIMEOUT");
  }
}

static bool keypadWriteRead(uint8_t outputValue, uint8_t& inputValue) {
  touchWire.beginTransmission(keypadAddress);
  touchWire.write(outputValue);
  if (touchWire.endTransmission() != 0) return false;
  delayMicroseconds(250);
  const int received = touchWire.requestFrom(static_cast<int>(keypadAddress), 1);
  if (received != 1 || !touchWire.available()) return false;
  inputValue = touchWire.read();
  return true;
}

static void keypadReleaseAll() {
  uint8_t ignored = 0xFF;
  keypadWriteRead(0xFF, ignored);
}

// Returns encoded pair 0xAB for P(A)<->P(B), -1 for no key, -2 for multiple
// closed pairs, and -3 for an I2C communication error.
static int scanKeypadPair() {
  int foundPair = -1;
  uint8_t pairCount = 0;
  for (uint8_t first = 0; first < 8; ++first) {
    const uint8_t drive = static_cast<uint8_t>(0xFFU & ~(1U << first));
    uint8_t sample = 0xFF;
    if (!keypadWriteRead(drive, sample)) {
      keypadReleaseAll();
      return -3;
    }
    for (uint8_t second = first + 1; second < 8; ++second) {
      if ((sample & (1U << second)) == 0) {
        foundPair = (first << 4) | second;
        ++pairCount;
      }
    }
  }
  keypadReleaseAll();
  if (pairCount == 0) return -1;
  if (pairCount == 1) return foundPair;
  return -2;
}

static void printKeypadPair(uint8_t pair) {
  Serial.printf("P%u<->P%u", pair >> 4, pair & 0x0F);
}

static bool validateKeypadMapping() {
  uint8_t degree[8] = {0};
  for (uint8_t i = 0; i < KEYPAD_KEY_COUNT; ++i) {
    const uint8_t a = keypadPairs[i] >> 4;
    const uint8_t b = keypadPairs[i] & 0x0F;
    if (a >= 8 || b >= 8 || a == b) return false;
    for (uint8_t j = 0; j < i; ++j) {
      if (keypadPairs[j] == keypadPairs[i]) return false;
    }
    ++degree[a];
    ++degree[b];
  }
  uint8_t rows = 0, columns = 0, unused = 0;
  for (uint8_t pin = 0; pin < 8; ++pin) {
    if (degree[pin] == 3) ++rows;
    else if (degree[pin] == 4) ++columns;
    else if (degree[pin] == 0) ++unused;
    else return false;
  }
  return rows == 4 && columns == 3 && unused == 1;
}

static bool loadKeypadMapping() {
  Preferences storage;
  storage.begin(KEYPAD_NVS_NAMESPACE, true);
  const bool present = storage.getUInt("magic", 0) == KEYPAD_MAPPING_MAGIC &&
                       storage.getBytesLength("pairs") == sizeof(keypadPairs);
  if (present) storage.getBytes("pairs", keypadPairs, sizeof(keypadPairs));
  storage.end();
  keypadMappingReady = present && validateKeypadMapping();
  return keypadMappingReady;
}

static bool saveKeypadMapping() {
  if (!validateKeypadMapping()) return false;
  Preferences storage;
  if (!storage.begin(KEYPAD_NVS_NAMESPACE, false)) return false;
  const size_t pairBytes = storage.putBytes("pairs", keypadPairs, sizeof(keypadPairs));
  const size_t magicBytes = storage.putUInt("magic", KEYPAD_MAPPING_MAGIC);
  storage.end();
  keypadMappingReady = pairBytes == sizeof(keypadPairs) && magicBytes == sizeof(uint32_t);
  return keypadMappingReady;
}

static void drawKeypadCalibration(uint8_t index, const char* note, uint16_t accent = C_HEADER) {
  tft.fillScreen(C_BG);
  drawIndustrialHeader(accent);
  drawPanel(12, 52, SW - 24, 246, C_PANEL_2);
  printCenteredFit("HIỆU CHỈNH PHÍM", 68, 2, C_TEXT, true);
  char prompt[24];
  snprintf(prompt, sizeof(prompt), "BẤM PHÍM %c", keypadCalibrationKeys[index]);
  printCenteredFit(prompt, 128, 3, accent, true);
  char progress[24];
  snprintf(progress, sizeof(progress), "%u / %u", index + 1, KEYPAD_KEY_COUNT);
  printCenteredFit(progress, 190, 2, C_TEXT, true);
  if (note && note[0]) printCenteredFit(note, 238, 1, C_MUTED, false);
  printCenteredFit("BẤM VÀ THẢ TỪNG PHÍM", 270, 1, C_MUTED, false);
}

static void waitKeypadRelease() {
  uint32_t stableSince = 0;
  while (true) {
    const int pair = scanKeypadPair();
    if (pair == -1) {
      if (stableSince == 0) stableSince = millis();
      if (millis() - stableSince >= 100) return;
    } else {
      stableSince = 0;
    }
    delay(10);
  }
}

static int waitStableKeypadPair() {
  int candidate = -1;
  uint32_t stableSince = 0;
  uint32_t lastWarningAt = 0;
  while (true) {
    const int pair = scanKeypadPair();
    if (pair >= 0) {
      if (pair != candidate) {
        candidate = pair;
        stableSince = millis();
      } else if (millis() - stableSince >= 60) {
        return pair;
      }
    } else {
      candidate = -1;
      stableSince = 0;
      if ((pair == -2 || pair == -3) && millis() - lastWarningAt >= 1000) {
        Serial.println(pair == -2 ? "[KEYPAD CAL] Chi bam mot phim." :
                                    "[KEYPAD CAL] Loi giao tiep I2C.");
        lastWarningAt = millis();
      }
    }
    delay(10);
  }
}

static bool calibrateKeypadInteractive() {
  Serial.println("[KEYPAD CAL] Bat dau hieu chinh 12 phim; bam va tha theo man hinh.");
  while (true) {
    memset(keypadPairs, 0xFF, sizeof(keypadPairs));
    waitKeypadRelease();
    for (uint8_t index = 0; index < KEYPAD_KEY_COUNT; ++index) {
      while (true) {
        drawKeypadCalibration(index, "DANG CHO PHIM");
        const int pair = waitStableKeypadPair();
        bool duplicate = false;
        for (uint8_t previous = 0; previous < index; ++previous) {
          if (keypadPairs[previous] == static_cast<uint8_t>(pair)) duplicate = true;
        }
        if (duplicate) {
          Serial.printf("[KEYPAD CAL] Phim %c trung cap ", keypadCalibrationKeys[index]);
          printKeypadPair(static_cast<uint8_t>(pair));
          Serial.println("; bam lai dung phim.");
          drawKeypadCalibration(index, "TRUNG PHIM - BAM LAI", C_ERR);
          waitKeypadRelease();
          delay(500);
          continue;
        }
        keypadPairs[index] = static_cast<uint8_t>(pair);
        Serial.printf("[KEYPAD CAL] %c = ", keypadCalibrationKeys[index]);
        printKeypadPair(keypadPairs[index]);
        Serial.println();
        waitKeypadRelease();
        break;
      }
    }
    if (validateKeypadMapping()) break;
    Serial.println("[KEYPAD CAL] Cau truc khong phai ma tran 3x4; thu lai tu dau.");
    drawSimple("HIỆU CHỈNH LỖI", "SAI MA TRẬN 3X4", "KIỂM TRA DÂY", "THỬ LẠI", C_ERR);
    delay(1800);
  }
  if (!saveKeypadMapping()) {
    Serial.println("[KEYPAD CAL] Khong luu duoc mapping vao NVS.");
    drawSimple("HIỆU CHỈNH LỖI", "KHÔNG LƯU ĐƯỢC", "KIỂM TRA NVS", "", C_ERR);
    delay(1800);
    return false;
  }
  drawSimple("HIỆU CHỈNH XONG", "KEYPAD 3X4", "ĐÃ LƯU BỘ NHỚ", "ĐANG HOÀN TẤT", C_OK);
  Serial.println("[KEYPAD CAL] Thanh cong va da luu NVS.");
  delay(1000);
  return true;
}

// Queue calibration instead of starting it inside a WebServer callback or in
// the middle of parsing a Serial line. The guided calibration is intentionally
// exclusive: while it is active, scanner, keypad, touch and MES transactions
// are paused so no production input can be mixed into the new mapping.
static bool requestRuntimeKeypadCalibration(const char* source) {
  if (!keypadAvailable || keypadAddress == 0) {
    Serial.println("[KEYPAD CAL] Tu choi: khong tim thay PCF8574T.");
    return false;
  }
  if (keypadCalibrationRequested || keypadCalibrationInProgress) {
    Serial.println("[KEYPAD CAL] Tu choi: dang co mot lan hieu chinh.");
    return false;
  }
  if (hasPendingTransaction()) {
    Serial.println("[KEYPAD CAL] Tu choi: dang co giao dich cho dong bo.");
    return false;
  }
  if (uiState != UiState::READY) {
    Serial.printf("[KEYPAD CAL] Tu choi: kiosk dang o state=%s, can READY.\n",
                  stateName(uiState));
    return false;
  }

  keypadCalibrationRequested = true;
  safeCopy(keypadCalibrationRequestSource,
           sizeof(keypadCalibrationRequestSource),
           source && source[0] ? source : "UNKNOWN");
  return true;
}

static void serviceRuntimeKeypadCalibration() {
  if (!keypadCalibrationRequested || keypadCalibrationInProgress) return;

  keypadCalibrationRequested = false;
  keypadCalibrationInProgress = true;
  Serial.printf("[KEYPAD CAL] Bat dau khi kiosk dang chay; source=%s.\n",
                keypadCalibrationRequestSource);

  // Discard scanner bytes that arrived just before calibration. New scanner
  // input is not consumed until calibration has completed.
  scannerFrameLength = 0;
  while (ScannerSerial.available() > 0) ScannerSerial.read();

  clearRuntimeSelection();
  demoGoodQty = 0;
  demoReworkQty = 0;
  demoDefectQty = 0;
  keypadNumberLength = 0;
  keypadNumberBuffer[0] = '\0';
  clearKeypadCalibration();
  keypadReleaseAll();

  drawSimple("HIỆU CHỈNH KEYPAD", "TẠM KHÓA THAO TÁC", "LÀM THEO MÀN HÌNH", "KHÔNG TẮT NGUỒN", C_INFO);
  delay(700);
  const bool calibrated = calibrateKeypadInteractive();

  keypadCandidatePair = -1;
  keypadEmittedPair = -1;
  keypadCandidateSince = millis();
  keypadLastPollAt = millis();
  keypadBufferState = 0xFF;
  while (ScannerSerial.available() > 0) ScannerSerial.read();

  rt.lastError[0] = '\0';
  setUi(UiState::READY);
  lastUserActionAt = millis();
  keypadCalibrationInProgress = false;
  keypadCalibrationRequestSource[0] = '\0';

  if (calibrated) {
    Serial.println("[KEYPAD CAL] Hieu chinh runtime thanh cong; kiosk da tro ve READY.");
  } else {
    Serial.println("[KEYPAD CAL] Hieu chinh runtime that bai; keypad bi vo hieu hoa.");
  }
}

static char keypadPairToKey(uint8_t pair) {
  for (uint8_t i = 0; i < KEYPAD_KEY_COUNT; ++i) {
    if (keypadPairs[i] == pair) return keypadCalibrationKeys[i];
  }
  return '\0';
}

bool initKeypad() {
  keypadAvailable = false;
  keypadMappingReady = false;
  keypadAddress = 0;
  for (uint8_t address = KEYPAD_ADDRESS_FIRST; address <= KEYPAD_ADDRESS_LAST; ++address) {
    touchWire.beginTransmission(address);
    if (touchWire.endTransmission() == 0) {
      keypadAddress = address;
      break;
    }
  }
  if (keypadAddress == 0) {
    Serial.println("[KEYPAD] Khong tim thay PCF8574T tai 0x20..0x27; tiep tuc khong keypad.");
    return false;
  }
  keypadAvailable = true;
  keypadReleaseAll();
  if (!loadKeypadMapping()) {
    Serial.println("[KEYPAD] Chua co mapping hop le; mo hieu chinh tren LCD.");
    calibrateKeypadInteractive();
  }
  keypadCandidatePair = -1;
  keypadEmittedPair = -1;
  keypadCandidateSince = millis();
  Serial.printf("[KEYPAD READY] 3x4 PCF8574T=0x%02X mapping=%s SDA=GPIO%d SCL=GPIO%d\n",
                keypadAddress, keypadMappingReady ? "CALIBRATED" : "DISABLED",
                TOUCH_SDA, TOUCH_SCL);
  Serial.println("[KEYPAD MAP] 0..9=nhap so, #=OK, *=xoa; tai XAC NHAN: 1/#=OK, 2/*=SUA.");
  return keypadMappingReady;
}

static bool detectBootWifiSetupShortcut() {
  if (!keypadAvailable || !keypadMappingReady) return false;
  const int firstPair = scanKeypadPair();
  if (firstPair < 0 || keypadPairToKey(static_cast<uint8_t>(firstPair)) != '*') return false;
  const uint32_t started = millis();
  while (millis() - started < WIFI_SETUP_BOOT_VERIFY_MS) {
    const int pair = scanKeypadPair();
    if (pair < 0 || keypadPairToKey(static_cast<uint8_t>(pair)) != '*') return false;
    delay(10);
  }
  return true;
}

static bool isQuantityInputState() {
  return uiState == UiState::INPUT_GOOD || uiState == UiState::INPUT_REWORK || uiState == UiState::INPUT_DEFECT;
}

static int currentQuantityValue() {
  if (uiState == UiState::INPUT_REWORK) return demoReworkQty;
  return uiState == UiState::INPUT_DEFECT ? demoDefectQty : demoGoodQty;
}

static void redrawKeypadQuantity() {
  if (uiState == UiState::INPUT_GOOD) drawQtyInput("SẢN PHẨM ĐẠT", demoGoodQty);
  else if (uiState == UiState::INPUT_DEFECT) drawQtyInput("SẢN PHẨM LỖI", demoDefectQty);
  else if (uiState == UiState::INPUT_REWORK) drawQtyInput("LỖI SỬA ĐƯỢC", demoReworkQty);
}

static void clearKeypadNumber() {
  keypadNumberLength = 0;
  keypadNumberBuffer[0] = '\0';
  if (uiState == UiState::INPUT_GOOD) demoGoodQty = 0;
  else if (uiState == UiState::INPUT_REWORK) demoReworkQty = 0;
  else if (uiState == UiState::INPUT_DEFECT) demoDefectQty = 0;
}

static void backspaceKeypadNumber() {
  if (keypadNumberLength > 0) {
    --keypadNumberLength;
    keypadNumberBuffer[keypadNumberLength] = '\0';
  }
  const int value = keypadNumberLength > 0 ? atoi(keypadNumberBuffer) : 0;
  if (uiState == UiState::INPUT_GOOD) demoGoodQty = value;
  else if (uiState == UiState::INPUT_REWORK) demoReworkQty = value;
  else if (uiState == UiState::INPUT_DEFECT) demoDefectQty = value;
}

static void appendKeypadDigit(char key) {
  // A new entry replaces the default zero. Leading zeroes are collapsed.
  if (keypadNumberLength == 1 && keypadNumberBuffer[0] == '0') {
    keypadNumberLength = 0;
  }
  if (keypadNumberLength >= sizeof(keypadNumberBuffer) - 1) {
    Serial.println("[KEYPAD] Da dat gioi han 6 chu so (999999).");
    return;
  }
  keypadNumberBuffer[keypadNumberLength++] = key;
  keypadNumberBuffer[keypadNumberLength] = '\0';
  const int value = atoi(keypadNumberBuffer);
  if (uiState == UiState::INPUT_GOOD) demoGoodQty = value;
  else if (uiState == UiState::INPUT_REWORK) demoReworkQty = value;
  else if (uiState == UiState::INPUT_DEFECT) demoDefectQty = value;
  redrawKeypadQuantity();
}

void handleKeypadKey(char key) {
  lastUserActionAt = millis();
  stateEnteredAt = millis();  // quantity timeout is idle time, not total screen time
  lastInputAtMs = millis();
  ++inputEventCount;
  lastInputWasVirtual = false;
  lastInputWasKeypad = true;
  snprintf(lastInputPreview, sizeof(lastInputPreview), "KEYPAD '%c'", key);
  Serial.printf("[KEYPAD] phim='%c' state=%s\n", key, stateName(uiState));

  if (maintenanceMode) {
    if (key == '1') {
      maintenanceWeb.stop(); maintenanceWebActive = false;
      drawSimple("CÀI ĐẶT WI-FI", "ĐANG KHỞI ĐỘNG...", "", "", C_INFO);
      startSetupPortal("Maintenance WiFi setup");
    } else if (key == '2') {
      lastOfflineSyncAt=0;syncOneOfflineEvent();drawMaintenanceScreen();
    } else if (key == '#') {
      exitMaintenanceMode();
    }
    return;
  }

  if (uiState == UiState::ERROR_STATE) {
    // Error screen is already a safe, non-production state. Accept both the
    // advertised back key and any keypad key so a held/sticky '*' cannot trap
    // the kiosk in the maintenance-hold path.
    recoverUiFromStuck(key == '*' ? "USER_DISMISSED_ERROR" : "USER_DISMISSED_ERROR_KEY", true);
    return;
  }

  if (uiState == UiState::ASK_REWORK) {
    if (key == '1' || key == '2') handleSerialLine(String(key));
    else if (key == '*') setUi(UiState::INPUT_DEFECT);
    else Serial.println("[KEYPAD] Chon 1 KHONG hoac 2 CO LOI SUA DUOC.");
    return;
  }

  if (uiState == UiState::FINISH_RETRY) {
    if (key == '1' || key == '#') handleSerialLine("1");
    else if (key == '2' || key == '*') handleSerialLine("2");
    return;
  }

  if (uiState == UiState::CONFIRM_QTY) {
    if (key == '1' || key == '#') {
      handleSerialLine("1");
    } else if (key == '2' || key == '*') {
      handleSerialLine("2");
    } else {
      Serial.println("[KEYPAD] Man XAC NHAN chi nhan 1/# hoac 2/*.");
    }
    return;
  }

  if (!isQuantityInputState()) {
    Serial.println("[KEYPAD] Bo qua: kiosk khong o man hinh nhap so luong.");
    return;
  }

  if (key >= '0' && key <= '9') {
    appendKeypadDigit(key);
    return;
  }

  if (key == '*') {
    backspaceKeypadNumber();
    redrawKeypadQuantity();
    return;
  }

  if (key == '#') {
    const int value = keypadNumberLength > 0 ? atoi(keypadNumberBuffer) : currentQuantityValue();
    clearKeypadNumber();
    handleSerialLine(String(value));
    return;
  }

  Serial.println("[KEYPAD] Phim A/B/C/D khong duoc gan chuc nang.");
}

void serviceKeypad() {
  if (!keypadAvailable || !keypadMappingReady || millis() - keypadLastPollAt < 12) return;
  keypadLastPollAt = millis();

  const uint8_t stateNow = static_cast<uint8_t>(uiState);
  if (keypadBufferState != stateNow) {
    keypadBufferState = stateNow;
    keypadNumberLength = 0;
    keypadNumberBuffer[0] = '\0';
  }

  const int pair = scanKeypadPair();
  if (pair != keypadCandidatePair) {
    keypadCandidatePair = pair;
    keypadCandidateSince = millis();
    return;
  }

  if (millis() - keypadCandidateSince < 40) return;

  const char stableKey = keypadCandidatePair >= 0
      ? keypadPairToKey(static_cast<uint8_t>(keypadCandidatePair)) : '\0';
  if (wifiSetupHoldActive) {
    if (stableKey == '*') {
      if (!wifiSetupHoldTriggered && millis() - wifiSetupHoldStartedAt >= WIFI_SETUP_HOLD_MS) {
        wifiSetupHoldTriggered = true;
        enterMaintenanceMode(false);
      }
    } else if (keypadCandidatePair == -1) {
      // A normal '*' action is deferred until release so a maintenance hold
      // never deletes/cancels anything while its ten-second timer is active.
      if (!wifiSetupHoldTriggered) handleKeypadKey('*');
      wifiSetupHoldActive = false;
      wifiSetupHoldTriggered = false;
      wifiSetupHoldStartedAt = 0;
      keypadEmittedPair = -1;
    } else {
      // Another key or an invalid multi-key state cancels the hold completely.
      wifiSetupHoldActive = false;
      wifiSetupHoldTriggered = false;
      wifiSetupHoldStartedAt = 0;
    }
    return;
  }

  if (stableKey == '*') {
    wifiSetupHoldActive = true;
    wifiSetupHoldTriggered = false;
    wifiSetupHoldStartedAt = millis();
    keypadEmittedPair = keypadCandidatePair;
    return;
  }

  if (keypadCandidatePair >= 0 && keypadCandidatePair != keypadEmittedPair) {
    const char key = keypadPairToKey(static_cast<uint8_t>(keypadCandidatePair));
    if (key != '\0') {
      handleKeypadKey(key);
    } else {
      Serial.print("[KEYPAD] Cap chan chua duoc map: ");
      printKeypadPair(static_cast<uint8_t>(keypadCandidatePair));
      Serial.println();
    }
    keypadEmittedPair = keypadCandidatePair;
  } else if (keypadCandidatePair == -1) {
    keypadEmittedPair = -1;
  } else if (keypadCandidatePair == -2 && keypadEmittedPair != -2) {
    Serial.println("[KEYPAD] Nhieu phim cung luc.");
    keypadEmittedPair = -2;
  } else if (keypadCandidatePair == -3 && keypadEmittedPair != -3) {
    Serial.println("[KEYPAD] Loi giao tiep I2C voi PCF8574T.");
    keypadEmittedPair = -3;
  }
}

void maintainConnection() {
  static uint32_t lastWifiAttempt = 0;
  static uint32_t disconnectObservedAt = 0;
  constexpr uint32_t WIFI_TRANSIENT_GRACE_MS = 12000;
  constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 20000;

  const bool connected = WiFi.status() == WL_CONNECTED;
  if (!connected) {
    otaLinkReady = false;
    if (!disconnectObservedAt) disconnectObservedAt = millis();
    const uint32_t disconnectedFor = millis() - disconnectObservedAt;

    // Ignore short status transitions caused by closing an HTTP socket. This
    // prevents FINISH from flashing a Wi-Fi error and restarting the station.
    if (disconnectedFor < WIFI_TRANSIENT_GRACE_MS) return;

    recordServerResult(false,true,-99,"WIFI");
    if (!wifiLostAt) {
      wifiLostAt = disconnectObservedAt;
      // Count it only once per outage, and only after the transient grace
      // period above -- a status blip while an HTTP socket closes is not an
      // outage and must not inflate this counter (that distinction is the
      // whole reason WIFI_TRANSIENT_GRACE_MS exists).
      if (wifiDropCount < 65535) wifiDropCount++;
      lastWifiDropAt = disconnectObservedAt;
    }
    if (!offlineMode && millis() - wifiLostAt >= OFFLINE_ENTER_AFTER_MS) {
      offlineMode = true;
      Serial.println("[NETWORK] Chuyen sang OFFLINE sau 45 giay mat mang.");
      if (uiState == UiState::READY) drawReady(0);
    }

    // Never call WiFi.disconnect() here: it turns a transient status change
    // into a real disconnect. Let the ESP-IDF auto reconnect first, then issue
    // a non-destructive reconnect request at a controlled interval.
    if (millis() - lastWifiAttempt >= WIFI_RECONNECT_INTERVAL_MS) {
      lastWifiAttempt = millis();
      Serial.printf("[NETWORK] WiFi mat %lu ms, yeu cau reconnect. status=%d\n",
                    static_cast<unsigned long>(disconnectedFor),
                    static_cast<int>(WiFi.status()));
      WiFi.reconnect();
    }
    return;
  }

  // Close out a confirmed outage exactly once, before wifiLostAt is cleared.
  if (wifiLostAt) {
    lastWifiRecoveredAt = millis();
    const uint32_t outage = lastWifiRecoveredAt - wifiLostAt;
    if (outage > longestWifiOutageMs) longestWifiOutageMs = outage;
    Serial.printf("[NETWORK] WiFi tro lai sau %lu ms (lan mat thu %u).\n",
                  static_cast<unsigned long>(outage), static_cast<unsigned>(wifiDropCount));
  }
  disconnectObservedAt = 0;
  wifiLostAt = 0;
  if(serverLinkState==ServerLinkState::WIFI_DOWN)serverLinkState=ServerLinkState::UNKNOWN;
  rt.online=serverLinkState==ServerLinkState::AVAILABLE;
  if (!rt.bound && AUTO_BIND) {
    static uint32_t lastBindAttempt = 0;
    if (millis() - lastBindAttempt >= 15000) {
      lastBindAttempt = millis();
      const bool boundNow = bindKiosk();
      if (!boundNow && uiState == UiState::BINDING) setUi(UiState::READY);
    }
    // Do not block scanner/UI while waiting for station binding.
    return;
  }
  // A newly restored MES link gets an immediate OTA check instead of waiting
  // for the normal cooldown.  This also covers bind completion after boot.
  if (rt.bound && rt.online && !otaLinkReady) {
    otaLinkReady = true;
    lastOtaCheckAt = 0;
    otaCheckSucceeded = false;
    otaAvailableWaitingIdle = false;
    Serial.println("[OTA] LINK_READY immediate check scheduled");
  }
  if (rt.bound && hasPendingTransaction()) {
    if (millis() - lastPendingRetryAt >= PENDING_RETRY_MS) { lastPendingRetryAt = millis(); syncPendingTransaction(false); }
    return;
  }
  uint16_t queued = countPendingOfflineEvents();
  if (rt.bound && queued > 0) {
    offlineMode = true; // remain visibly offline/syncing until durable ACKs complete
    static const uint32_t retryDelays[] = {5000,10000,30000,60000,120000};
    if (!offlineNextSyncAt || static_cast<int32_t>(millis()-offlineNextSyncAt)>=0) {
      lastOfflineSyncAt = millis();
      if(syncOneOfflineEvent()) offlineSyncFailures=0; else if(offlineSyncFailures<4) offlineSyncFailures++;
      const uint32_t base=retryDelays[min<uint8_t>(offlineSyncFailures,4)];
      const uint32_t jitter=base/10U+(esp_random()%(base/10U+1U));
      offlineNextSyncAt=millis()+base+jitter;
      if (countPendingOfflineEvents() == 0) {
        offlineNextSyncAt=0;
        offlineMode = false;
        Serial.println("[OFFLINE SYNC] Queue da dong bo het; tro lai ONLINE.");
        if (uiState == UiState::READY) drawReady(0);
      }
    }
    return;
  }
  offlineMode = false;
  if(rt.bound&&uiState==UiState::READY&&!hasPendingTransaction()&&countPendingOfflineEvents()==0){
    const bool due=workerCacheCount==0||operationCacheCount==0||!lastCatalogAutoRefreshAt||millis()-lastCatalogAutoRefreshAt>=6UL*60UL*60UL*1000UL;
    if(due&&millis()-lastCatalogAutoAttemptAt>=60000UL){
      lastCatalogAutoAttemptAt=millis();String message;uint16_t workers=0,operations=0;
      if(refreshCatalogFromMes(message,workers,operations)){lastCatalogAutoRefreshAt=millis();Serial.printf("[OFFLINE] snapshot revision=%s workers=%u operations=%u\n",offlineSnapshotRevision,workers,operations);}
      else Serial.printf("[OFFLINE] snapshot refresh failed: %s\n",message.c_str());
    }
  }
  if (rt.bound && canSendHeartbeat() && millis() - lastHeartbeatAt >= HEARTBEAT_MS) { lastHeartbeatAt = millis(); sendHeartbeat(); }
  scheduleOtaCheck();
}

// ============================================================
// Arduino setup/loop
// ============================================================
void setup() {
  // Capture BEFORE anything else can reset it. This is the one fact that
  // separates "the watchdog below force-rebooted us" (TASK-WDT) from "the
  // supply sagged when the scanner lit up" (BROWNOUT) from "somebody power
  // cycled it" (POWERON) -- see the FORENSICS block near wifiDropCount.
  bootResetReason = esp_reset_reason();
  bootAtMs = millis();

  // SAFETY NET (found 2026-08-22, round 5): DNS resolution inside
  // NetworkClient::connect() -> Network.hostByName() -> lwip_getaddrinfo()
  // is NOT bounded by httpGetJson()/httpPostJson()'s connectTimeoutMs /
  // requestTimeoutMs at all -- those only start counting after DNS already
  // resolved. lwIP retries DNS against up to CONFIG_LWIP_DNS_MAX_SERVERS(3)
  // servers with its own internal backoff, which on a degraded/flaky WiFi
  // DNS path can genuinely block the single-threaded main loop task for a
  // long time with zero recovery -- exactly the "quet OP treo >30s, khong
  // bao gio bao loi" symptom, on the kiosk's real WiFi where this sandbox's
  // curl tests can't reproduce it (different network path/DNS server).
  // A full async-DNS-with-hard-deadline rewrite is high-risk to ship
  // without any way to test it live here, so this is a bounded, standard
  // ESP-IDF safety net instead: if the main loop stops feeding the task
  // watchdog for this long (only possible while genuinely stuck inside one
  // blocking call), the device force-reboots instead of freezing forever.
  esp_task_wdt_config_t wdtConfig;
  wdtConfig.timeout_ms = 40000;
  wdtConfig.idle_core_mask = 0;
  wdtConfig.trigger_panic = true;
  if (esp_task_wdt_init(&wdtConfig) == ESP_ERR_INVALID_STATE) {
    // Already initialized by the framework's own default config; adopt our
    // timeout instead of erroring out.
    esp_task_wdt_reconfigure(&wdtConfig);
  }
  esp_task_wdt_add(NULL);

  Serial.begin(115200);
  pinMode(SCANNER_RX_PIN, INPUT_PULLUP);
  ScannerSerial.setRxBufferSize(1024);
  ScannerSerial.begin(SCANNER_BAUD, SERIAL_8N1, SCANNER_RX_PIN, -1);
  delay(100);
  while (ScannerSerial.available() > 0) ScannerSerial.read();
  delay(800);
  Serial.printf("\n[SERIAL READY] %s - baud 115200\n", APP_VERSION);
  // Name it, don't just number it: this line is what somebody reads back
  // over serial (or photographs) after a field reset, and "reset_reason=6"
  // means nothing to anyone standing at the machine. 6 = TASK-WDT (the 40s
  // safety net above fired on a blocked network call), 9 = BROWNOUT (supply
  // sag), 1 = POWERON (power cut / plug pulled), 3 = SW (our own restart).
  Serial.printf("[BOOT] reset_reason=%s (%d) free_heap=%u\n",
                resetReasonName(bootResetReason),
                static_cast<int>(bootResetReason),
                static_cast<unsigned>(ESP.getFreeHeap()));
  Serial.printf("[SCANNER READY] UART1 RX=GPIO%d TX=DISABLED baud=%lu 8N1 inverted=NO frame_timeout=%lums\n",
                SCANNER_RX_PIN,
                static_cast<unsigned long>(SCANNER_BAUD),
                static_cast<unsigned long>(SCANNER_FRAME_TIMEOUT_MS));
  Serial.println("[SCANNER WIRING] GM865 TX -> ESP GPIO44/RX; GM865 RX de trong; GND chung; VCC 5V.");

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  lcdSPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin(20000000);
  tft.invertDisplay(true);
  delay(20);
  tft.setRotation(LCD_ROTATION);
  if (tft.width() > tft.height()) tft.setRotation(0);
  tft.invertDisplay(true);
  tft.setTextWrap(false);
#if MESFLOW_UI_SCREENSHOT
  uiShadowFramebuffer = static_cast<uint16_t*>(heap_caps_malloc(
      static_cast<size_t>(SW) * SH * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (uiShadowFramebuffer) {
    memset(uiShadowFramebuffer, 0, static_cast<size_t>(SW) * SH * sizeof(uint16_t));
    Serial.printf("[UI CAPTURE] RGB565 shadow framebuffer=%u bytes PSRAM\n",
                  static_cast<unsigned>(static_cast<size_t>(SW) * SH * sizeof(uint16_t)));
  } else {
    Serial.println("[UI CAPTURE] Shadow framebuffer allocation failed; LCD RAMRD fallback only.");
  }
#endif
  tft.fillScreen(C_BG);
  Serial.printf("[DISPLAY] ILI9341 %dx%d rotation=%u inversion=ON bg=0x%04X primary=0x%04X\n",
                tft.width(), tft.height(), LCD_ROTATION, C_BG, C_HEADER);
  initTouch();
  initKeypad();
  bootWifiSetupRequested = detectBootWifiSetupShortcut();
  setUi(UiState::BOOT);
  lastUserActionAt = millis();
  lastLoopAliveAt = millis();

  if (!loadOrCreateDeviceIdentity()) {
    safeCopy(rt.lastError, sizeof(rt.lastError), "DEVICE_IDENTITY_INIT_FAILED");
    drawSimple("LỖI DANH TÍNH", "KHÔNG TẠO ĐƯỢC ID", "KIỂM TRA NVS", "", C_ERR);
    return;
  }

  loadDeviceConfig();
  if (PREFER_PLAIN_HTTP_FOR_MESFLOW && String(SERVER_BASE).equalsIgnoreCase("https://mesflow.net")) {
    safeCopy(SERVER_BASE, sizeof(SERVER_BASE), "http://mesflow.net");
    saveDeviceConfig();
    Serial.println("[CONFIG] Switched mesflow.net transport HTTPS -> HTTP to reduce TLS memory usage.");
  }
  Serial.printf("[CONFIG] SSID=%s server=%s device=%s station=%s identity=%s\n",
                WIFI_SSID[0] ? WIFI_SSID : "<NOT SET>",
                SERVER_BASE[0] ? SERVER_BASE : "<NOT SET>", DEVICE_ID, STATION_CODE, DEVICE_UUID);

  // Missing service configuration never opens the maintenance AP automatically.
  if (!hasServerConfig()) {
    safeCopy(rt.lastError, sizeof(rt.lastError), "SERVER_URL_NOT_CONFIGURED");
    Serial.println("[CONFIG ERROR] Chua cau hinh MES Server URL.");
    setUi(UiState::OFFLINE);
  }

  prefs.begin("mesflow", true);
  String storedToken = prefs.getString("token", "");
  String storedStation = prefs.getString("station", "");
  prefs.end();

  demoGoodQty = 0;
  demoReworkQty = 0;
  demoDefectQty = 0;
  bootId = esp_random();
  Serial.printf("[MEM] Internal heap free=%u; PSRAM found=%s total=%u free=%u\n",
                static_cast<unsigned>(ESP.getFreeHeap()),
                psramFound() ? "YES" : "NO",
                static_cast<unsigned>(ESP.getPsramSize()),
                static_cast<unsigned>(ESP.getFreePsram()));
  allocateOfflineBuffers();
  fsReady = LittleFS.begin(true);
  prefs.begin("mesflow", true);
  clientEventCounter = prefs.getULong("evt_counter", 0);
  safeCopy(offlineSnapshotRevision,sizeof(offlineSnapshotRevision),prefs.getString("snap_rev","unknown").c_str());
  prefs.end();
  emitActionEvent("KIOSK_BOOT", "SYSTEM", "SUCCESS");
  if (!fsReady) Serial.println("[OFFLINE] LittleFS mount FAIL; offline mode disabled.");
  else if (!offlineBuffersReady) Serial.println("[OFFLINE] PSRAM buffers unavailable; online mode only.");
  else { loadOfflineStorage(); recoverOfflineSessionIntents(); }
  loadPendingTransaction();

  if (storedToken.length() && storedStation == STATION_CODE) {
    safeCopy(rt.kioskToken, sizeof(rt.kioskToken), storedToken.c_str());
    rt.bound = true;
    Serial.println("Da nap kiosk token tu NVS.");
  }

  if (!WIFI_SSID[0]) {
    rt.online = false;
    setUi(UiState::OFFLINE);
    if (bootWifiSetupRequested) enterMaintenanceMode(true);
    return;
  }

  if (!connectWifi()) {
    rt.online = false;
    setUi(UiState::OFFLINE);
    if (bootWifiSetupRequested) enterMaintenanceMode(true);
    return;
  } else {
    configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");
    startLanProvisioning();
  }

  if (bootWifiSetupRequested) {
    setUi(UiState::READY);
    enterMaintenanceMode(true);
    return;
  }

  if (WiFi.status() == WL_CONNECTED && AUTO_BIND && !rt.bound) {
    // Initial bind is best-effort only. A server error must not block the UI.
    // maintainConnection() retries every 15 seconds in the background.
    if (!bindKiosk()) {
      Serial.println("[STARTUP] Bind chua thanh cong; tiep tuc READY va retry nen.");
    }
  }

  if (hasPendingTransaction()) {
    setUi(UiState::SYNC_PENDING);
    lastPendingRetryAt = millis() - PENDING_RETRY_MS;
    syncPendingTransaction(false);
  } else {
    setUi(UiState::READY);
    lastHeartbeatAt = millis() - HEARTBEAT_MS;
    sendHeartbeat();
    if (rt.online) confirmPendingOtaBoot();
    lastHeartbeatAt = millis();
    Serial.println("[READY] Quet the nhan vien.");
  }
}

void loop() {
  lastLoopAliveAt = millis();
  esp_task_wdt_reset();

  // Execute FINISH cleanup in a fresh loop iteration, after the HTTP request
  // and local ArduinoJson documents from syncPendingTransaction() are gone.
  if (deferredFinishReset) {
    deferredFinishReset = false;
    Serial.printf("[FINISH] deferred reset begin. heap=%u min_heap=%u stack_free_words=%u\n",
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMinFreeHeap()),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    resetForNextWorker();
    Serial.printf("[FINISH] deferred reset complete. heap=%u stack_free_words=%u\n",
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  }

  // Serve LAN Web Console first so button presses are accepted with minimal latency.
  serviceDeviceManagement();
  if (setupPortalActive) return;
  if (maintenanceMode) {
    serviceKeypad();
    serviceMaintenanceMode();
    static uint32_t lastMaintenanceReconnectAt = 0;
    if (WiFi.status() != WL_CONNECTED && millis() - lastMaintenanceReconnectAt >= 10000) {
      lastMaintenanceReconnectAt = millis(); WiFi.reconnect();
    }
    delay(2);
    return;
  }
  servicePendingWebCommand();
  readSerialCommands();
  serviceRuntimeKeypadCalibration();
  readScannerCommands();
  serviceKeypad();
  handleTouch();
  serviceStateWatchdog();
  maintainConnection();
  // Background telemetry is lowest priority. Do not let its HTTP POST delay a
  // recent Web Console command or scanner interaction.
  if (millis() - lastWebCommandAcceptedAt > 8000) serviceActionEventQueue();
  if (uiState != UiState::TOUCH_TEST) { drawTopClock(); serviceNetworkIndicator(); }

  // UI tinh: khong con chu QUET THE / QUET MA chay dau cham.
  // Man hinh chi redraw khi state thay doi de giu phong cach sach, on dinh.
  static uint8_t waitAnimationFrame = 0;

  // Luong toi gian: giu nguyen mot man hinh START_SUCCESS tinh trong 10 giay.
  // Khong ve countdown moi giay de tranh redraw va giam tai cho LCD/heap.
  if (uiState == UiState::START_SUCCESS &&
      millis() - stateEnteredAt >= START_SUCCESS_HOLD_MS) {
    releaseKioskAfterStart();
    waitAnimationFrame = 0;
  }

  if (uiState == UiState::ERROR_STATE && returnToReadyAfterError &&
      millis() - stateEnteredAt >= BUSINESS_ERROR_HOLD_MS) {
    returnToReadyAfterError = false;
    rt.lastHttpStatus = 0;
    rt.lastError[0] = '\0';
    waitAnimationFrame = 0;
    setUi(UiState::READY);
    emitActionEvent("RETURN_TO_READY", "UI_STATE", "SUCCESS");
    endSessionTrace();
  }

  delay(2);
}
