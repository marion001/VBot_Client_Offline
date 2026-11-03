//Code By: Vũ Tuyển
//Designed by: BootstrapMade
//GitHub VBot: https://github.com/marion001/VBot_Offline.git
//Facebook Group: https://www.facebook.com/groups/1148385343358824
//Facebook: https://www.facebook.com/TWFyaW9uMDAx
//Email: VBot.Assistant@gmail.com

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#ifndef ELEGANTOTA_USE_ASYNC_WEBSERVER
#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1
#endif
#include <ElegantOTA.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <lwip/sockets.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <AudioFileSourceHTTPStream.h>
#include <AudioFileSourceBuffer.h>
#include <AudioFileSourceLittleFS.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include "driver/i2s.h"
#include <new>
#include <atomic>
#include <utility>
#include <esp_heap_caps.h>

#if defined(CONFIG_FREERTOS_UNICORE) && CONFIG_FREERTOS_UNICORE
#error "VBot Client ESP32 yêu cầu chip ESP có CPU 2 core trở lên. Các chip có CPU 1 core không được hỗ trợ."
#endif

#define VBOT_I2S_COMM_FORMAT I2S_COMM_FORMAT_STAND_I2S

static const char *VBOT_CLIENT_VERSION = "20/09/2026";
static constexpr uint16_t DEFAULT_VBOT_API_PORT = 5002;
#ifndef VBOT_BUILD_TARGET
#define VBOT_BUILD_TARGET "ESP32"
#endif
#ifndef VBOT_UPDATE_FAMILY
#define VBOT_UPDATE_FAMILY "ESP32"
#endif

static constexpr uint32_t MIC_SAMPLE_RATE = 16000;
static constexpr size_t FRAME_SAMPLES = 512;
static constexpr size_t FRAME_BYTES = FRAME_SAMPLES * sizeof(int16_t);
static constexpr i2s_port_t MIC_I2S_PORT = I2S_NUM_1;
static constexpr i2s_port_t PCM_I2S_PORT = I2S_NUM_0;
static constexpr uint8_t WS_SEND_QUEUE_LEN = 8;
static constexpr uint8_t DAC_QUEUE_LEN = 12;
static constexpr size_t PCM_POOL_BUFFER_SIZE = 2048;
static constexpr size_t MP3_STREAM_BUFFER_SIZE = 8 * 1024;
static constexpr int I2S_DMA_BUFFER_COUNT = 12;
static constexpr int I2S_DMA_BUFFER_LENGTH = 256;
static constexpr uint32_t MIC_TASK_STACK = 3072;
static constexpr uint32_t WEBSOCKET_TASK_STACK = 6144;
static constexpr uint32_t DAC_TASK_STACK = 6144;
// FastLED trên ESP32-S3 cần nhiều stack hơn ESP32 thường. Stack 2048 có thể làm
// core chạy LED rơi vào panic/double exception trước khi setup() hoàn tất, sau
// đó TG1WDT reset toàn hệ thống mà chưa kịp in Guru Meditation/backtrace.
static constexpr uint32_t LED_TASK_STACK = 4096;
static constexpr uint32_t BUTTON_TASK_STACK = 3072;
static constexpr uint32_t WIFI_TASK_STACK = 4096;
static constexpr uint32_t UPDATE_TASK_STACK = 6144;
static constexpr uint32_t MIC_DEBUG_INTERVAL_MS = 3000;
static constexpr uint32_t VOLUME_LED_DURATION_MS = 1000;
static constexpr uint32_t VOLUME_ZERO_LED_DURATION_MS = 500;
static constexpr uint32_t VOLUME_SAVE_DELAY_MS = 1000;
static constexpr size_t GOOGLE_TTS_MAX_TEXT_BYTES = 240;
static constexpr uint32_t DAC_MP3_WARMUP_MS = 250;

struct AppConfig {
  char clientName[33] = "VBot Client Socket";
  char serverHost[64] = "192.168.1.10";
  uint16_t serverPort = 5003;
  uint16_t apiPort = DEFAULT_VBOT_API_PORT;
  char serverPath[32] = "/";
  char streamApiUrl[160] = "";
  bool useCustomStreamApiUrl = false;
  bool fallbackStreamApiToServer = false;
  char sessionId[48] = "esp32_vbot_client";
  char startupMode[16] = "vbot_server";
  char workingMode[20] = "main_processing";
  uint8_t volume = 70;
  bool ledEnabled = true;
  bool buttonsEnabled = true;
  char inputMode[12] = "button";
  uint8_t ledBrightnessPct = 80;
  uint16_t ledCount = 12;
  char ledThinkColor[8] = "#0000FF";
  char ledMuteColor[8] = "#FF8C00";
  int pinLed =
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    48;
#else
    23;
#endif
  int pinBtnMic =
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    41;
#else
    32;
#endif
  int pinBtnVolUp =
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    40;
#else
    33;
#endif
  int pinBtnVolDown =
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    39;
#else
    18;
#endif
  int pinBtnWake =
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    42;
#else
    19;
#endif
  int pinRotaryClk =
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    41;
#else
    32;
#endif
  int pinRotaryDt =
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    40;
#else
    33;
#endif
  int pinRotarySw =
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    42;
#else
    19;
#endif
  uint8_t rotaryVolumeStep = 2;
  int micBclk =
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    5;
#else
    14;
#endif
  int micWs =
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    4;
#else
    13;
#endif
  int micDin =
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    6;
#else
    34;
#endif
  char micChannel[2] = "L";
  int dacBclk =
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    15;
#else
    27;
#endif
  int dacLrc =
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    16;
#else
    26;
#endif
  int dacDout =
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    7;
#else
    25;
#endif
  bool micEnabled = true;
  bool hotwordStreamEnabled = true;
  bool conversationMode = true;
  bool startupSoundEnabled = true;
  bool serialLogs = true;
  bool mdnsEnabled = true;
  bool mqttEnabled = false;
  char mqttHost[64] = "";
  uint16_t mqttPort = 1883;
  char mqttUsername[64] = "";
  char mqttPassword[96] = "";
  char mqttClientName[40] = "VBot_Client_ESP32";
  float micGain = 1.0f;
  float pcmVolumeBoost = 1.0f;
  uint16_t buttonPressDebounceMs = 50;
  uint16_t buttonReleaseDebounceMs = 50;
  uint16_t buttonHoldMs = 2000;
  char wakeLongAction[20] = "stop_audio";
};

enum LedEffect {
  LED_SPEAK,
  LED_THINK,
  LED_LOADING,
  LED_MUTE,
  LED_ERROR,
  LED_SOCKET_ERROR,
  LED_STARTUP,
  LED_PAUSE,
  LED_VOLUME,
  LED_OFF
};

enum AudioCommandType {
  AUDIO_CMD_MP3_URL,
  AUDIO_CMD_MP3_FILE,
  AUDIO_CMD_GOOGLE_TTS,
  AUDIO_CMD_PCM_CHUNK,
  AUDIO_CMD_SET_VOLUME,
  AUDIO_CMD_STOP
};

struct MicFrame {
  uint8_t data[FRAME_BYTES];
};

struct AudioCommand {
  AudioCommandType type;
  char url[320];
  uint8_t *pcm = nullptr;
  int8_t pcmPoolSlot = -1;
  size_t pcmLen = 0;
  uint32_t sampleRate = 16000;
  bool clearSuspendedAudio = true;
  bool speakLed = true;
  bool autoContinueAfter = false;
  LedEffect playLedEffect = LED_SPEAK;
};

struct ButtonRuntime {
  bool rawPressed = false;
  bool stablePressed = false;
  bool longFired = false;
  uint32_t lastRawChangeMs = 0;
  uint32_t pressStartMs = 0;
};

struct RotaryRuntime {
  uint8_t lastState = 0;
  int8_t accumulator = 0;
  bool initialized = false;
};

AppConfig cfg;
Preferences prefs;
AsyncWebServer web(80);
WiFiClient mqttNetworkClient;
PubSubClient mqttClient(mqttNetworkClient);
bool mdnsStarted = false;
uint32_t mqttLastReconnectMs = 0;
uint32_t mqttLastPublishMs = 0;
uint32_t mqttLastStateCheckMs = 0;
uint32_t mqttLastStateHash = 0;
volatile bool mqttStateDirty = true;
volatile bool mqttNetworkBusy = false;
volatile bool mqttForceSyncRequested = false;
uint32_t wsBackpressureSinceMs = 0;
static constexpr uint8_t MQTT_STATE_CACHE_SIZE = 24;
struct MqttStateCacheEntry { String topic; String payload; };
MqttStateCacheEntry mqttStateCache[MQTT_STATE_CACHE_SIZE];
class VBotWebSocketsClient : public WebSocketsClient {
  static constexpr size_t MIC_WS_HEADER_SIZE = 8;
  uint8_t pendingMicFrame[MIC_WS_HEADER_SIZE + FRAME_BYTES];
  size_t pendingMicLength = 0;
  size_t pendingMicOffset = 0;
 public:
  bool canSendWithoutBlocking() {
    if (!_client.tcp || !_client.tcp->connected()) return false;
    int socketFd = _client.tcp->fd();
    if (socketFd < 0) return false;
    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(socketFd, &writeSet);
    timeval timeout = {0, 0};
    int ready = select(socketFd + 1, nullptr, &writeSet, nullptr, &timeout);
    return ready > 0 && FD_ISSET(socketFd, &writeSet);
  }

  void resetPendingMicFrame() {
    pendingMicLength = 0;
    pendingMicOffset = 0;
  }

  // 1=đã gửi hết, 0=TCP đang đầy, -1=socket lỗi. Không gọi
  // WiFiClient::write() vì hàm đó có thể chặn gần 10 giây khi EAGAIN.
  int flushPendingMicFrame() {
    if (pendingMicOffset >= pendingMicLength) {
      resetPendingMicFrame();
      return 1;
    }
    if (!_client.tcp || !_client.tcp->connected()) return -1;
    int socketFd = _client.tcp->fd();
    if (socketFd < 0) return -1;
    int sent = ::send(socketFd,
                      pendingMicFrame + pendingMicOffset,
                      pendingMicLength - pendingMicOffset,
                      MSG_DONTWAIT);
    if (sent > 0) {
      pendingMicOffset += (size_t)sent;
      if (pendingMicOffset >= pendingMicLength) {
        resetPendingMicFrame();
        return 1;
      }
      return 0;
    }
    if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return -1;
    return 0;
  }

  bool hasPendingMicFrame() const { return pendingMicLength != 0; }
  bool pendingMicFrameStarted() const { return pendingMicOffset != 0; }

  bool queueMicFrame(const uint8_t *payload, size_t length) {
    if (!payload || length != FRAME_BYTES || hasPendingMicFrame() ||
        !_client.tcp || !_client.tcp->connected()) return false;
    const uint32_t mask = esp_random();
    pendingMicFrame[0] = 0x82;  // FIN + binary opcode
    pendingMicFrame[1] = 0x80 | 126;
    pendingMicFrame[2] = (uint8_t)(length >> 8);
    pendingMicFrame[3] = (uint8_t)length;
    memcpy(pendingMicFrame + 4, &mask, 4);
    const uint8_t *maskBytes = pendingMicFrame + 4;
    for (size_t i = 0; i < length; ++i) {
      pendingMicFrame[MIC_WS_HEADER_SIZE + i] = payload[i] ^ maskBytes[i & 3];
    }
    pendingMicLength = MIC_WS_HEADER_SIZE + length;
    pendingMicOffset = 0;
    return flushPendingMicFrame() >= 0;
  }
};
VBotWebSocketsClient ws;
AudioGeneratorMP3 *mp3 = nullptr;
AudioFileSource *mp3File = nullptr;
AudioFileSource *mp3RawFile = nullptr;
AudioOutputI2S *mp3Out = nullptr;
CRGB *leds = nullptr;
String activeSessionId;
struct RestoreUploadContext {
  size_t length;
  bool tooLarge;
  char data[8193];
};
String currentAudioUrl;
String currentMqttMediaTitle;
String currentMqttMediaCover;
String currentGoogleTtsText;
volatile uint8_t googleTtsState = 0;  // 0=idle, 1=queued, 2=playing, 3=error
volatile uint32_t googleTtsUpdatedAtMs = 0;

QueueHandle_t micQueue = nullptr;
QueueHandle_t audioQueue = nullptr;
QueueHandle_t pcmFreeQueue = nullptr;
static uint8_t pcmPool[DAC_QUEUE_LEN][PCM_POOL_BUFFER_SIZE];
SemaphoreHandle_t cfgMutex = nullptr;
SemaphoreHandle_t mediaStateMutex = nullptr;
SemaphoreHandle_t nvsMutex = nullptr;
SemaphoreHandle_t micI2sMutex = nullptr;
SemaphoreHandle_t updateMutex = nullptr;
TaskHandle_t micTaskHandle = nullptr;
TaskHandle_t websocketTaskHandle = nullptr;
TaskHandle_t dacTaskHandle = nullptr;
TaskHandle_t ledTaskHandle = nullptr;
TaskHandle_t buttonTaskHandle = nullptr;
TaskHandle_t wifiTaskHandle = nullptr;
TaskHandle_t updateTaskHandle = nullptr;
bool updateCheckRunning = false;
bool updateCheckReady = false;
String updateCheckResult;
static int32_t micRawBuffer[FRAME_SAMPLES * 2];
static MicFrame micWorkFrame;
static MicFrame micDropFrame;
static MicFrame wsSendFrame;

volatile LedEffect ledEffect = LED_STARTUP;
volatile bool ledRefreshRequested = false;
volatile bool hardwareGpioValid = true;
String hardwareGpioErrors;
volatile bool fsMaintenanceActive = false;
volatile bool wsReady = false;
volatile uint32_t wsForceReconnectAtMs = 0;
uint32_t wsLastReconnectAttemptMs = 0;
volatile bool recording = false;
volatile uint32_t recordingDeadlineMs = 0;
volatile bool waitingToWake = true;
volatile bool pendingAutoContinue = false;
volatile bool clientConfigSyncRequested = false;
volatile bool temporaryMicEnabled = false;
volatile bool pcmTxInstalled = false;
volatile bool micRxInstalled = false;
volatile bool pcmPlaybackActive = false;
volatile bool audioOpening = false;
volatile bool startupAudioPlaying = false;
volatile bool micSuspendedForAudio = false;
volatile uint32_t lastPcmPlaybackMs = 0;
static constexpr uint32_t PCM_LED_HOLD_MS = 1200;
volatile uint32_t testLedUntilMs = 0;
volatile bool wakeSkipRequest = false;
bool pendingPcmBinary = false;
JsonDocument pendingPcmMeta;
bool pcmRawReceiveLogged = false;
bool localDingAvailable = false;
bool localDongAvailable = false;
bool localMicOnAvailable = false;
bool localMicOffAvailable = false;
bool localTutTutAvailable = false;
bool localMusicStopsAvailable = false;
bool localStartupAvailable = false;
volatile uint32_t restartAtMs = 0;
volatile bool otaUpdateActive = false;
bool restartBlockedLogged = false;
char restartReason[96] = "";
portMUX_TYPE restartMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t pendingWifiResetAtMs = 0;
volatile uint32_t pendingFactoryResetAtMs = 0;
volatile bool volumeSavePending = false;
volatile uint32_t volumeSaveAtMs = 0;
volatile uint8_t volumeSaveValue = 0;
volatile bool volumeSaveFeedbackPending = false;
volatile bool ledBrightnessSavePending = false;
volatile uint32_t ledBrightnessSaveAtMs = 0;
volatile uint8_t ledBrightnessSaveValue = 0;
volatile bool volumeLedActive = false;
volatile bool volumeZeroLedActive = false;
volatile uint8_t volumeLedValue = 0;
volatile uint32_t volumeLedUntilMs = 0;

#define LOGF(fmt, ...) do { if (cfg.serialLogs) Serial.printf("[%lu] " fmt "\n", millis(), ##__VA_ARGS__); } while (0)
#define LOG(msg) do { if (cfg.serialLogs) { Serial.print("["); Serial.print(millis()); Serial.print("] "); Serial.println(msg); } } while (0)
#define UART_LOGF(fmt, ...) do { Serial.printf("[%lu] " fmt "\n", millis(), ##__VA_ARGS__); } while (0)
#define UART_LOG(msg) do { Serial.print("["); Serial.print(millis()); Serial.print("] "); Serial.println(msg); } while (0)

static void lockMediaState() {
  if (mediaStateMutex) xSemaphoreTake(mediaStateMutex, portMAX_DELAY);
}

static void unlockMediaState() {
  if (mediaStateMutex) xSemaphoreGive(mediaStateMutex);
}

static void lockNvs() {
  if (nvsMutex) xSemaphoreTake(nvsMutex, portMAX_DELAY);
}

static void unlockNvs() {
  if (nvsMutex) xSemaphoreGive(nvsMutex);
}

static void lockMicI2s() {
  if (micI2sMutex) xSemaphoreTake(micI2sMutex, portMAX_DELAY);
}

static void unlockMicI2s() {
  if (micI2sMutex) xSemaphoreGive(micI2sMutex);
}

static bool deadlineReached(uint32_t deadline, uint32_t now = millis()) {
  return (int32_t)(now - deadline) >= 0;
}

static bool scheduleRestart(uint32_t delayMs, const char *reason) {
  if (otaUpdateActive) {
    UART_LOGF("Từ chối restart trong khi OTA đang chạy. Yêu cầu: %s", reason ? reason : "không rõ");
    return false;
  }
  uint32_t deadline = millis() + max(delayMs, 100U);
  portENTER_CRITICAL(&restartMux);
  restartAtMs = deadline;
  strlcpy(restartReason, reason && *reason ? reason : "Không rõ nguyên nhân", sizeof(restartReason));
  restartBlockedLogged = false;
  portEXIT_CRITICAL(&restartMux);
  UART_LOGF("Đã lên lịch khởi động lại sau %u ms. Lý do: %s", (unsigned)delayMs, restartReason);
  return true;
}

static void fatalHalt(const char *message) {
  Serial.printf("[%lu] LỖI NGHIÊM TRỌNG: %s; hệ thống đã dừng, restart/reset tự động sẽ không chạy. free=%u, min=%u\n",
                millis(), message ? message : "không rõ", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
  Serial.flush();
  for (;;) {
    delay(1000);
  }
}

class AudioFileSourceHTTPSStream : public AudioFileSource {
 public:
  AudioFileSourceHTTPSStream() {
    pos = 0;
    size = 0;
    saveURL[0] = 0;
    client.setInsecure();
  }

  explicit AudioFileSourceHTTPSStream(const char *url) : AudioFileSourceHTTPSStream() {
    open(url);
  }

  ~AudioFileSourceHTTPSStream() override {
    http.end();
  }

  bool open(const char *url) override {
    if (!url || !*url) return false;
    pos = 0;
    size = 0;
    http.end();
    client.setInsecure();
    client.setHandshakeTimeout(8);
    if (!http.begin(client, url)) {
      cb.st(STATUS_HTTPFAIL, PSTR("Không thể bắt đầu yêu cầu HTTPS"));
      return false;
    }
    http.setTimeout(8000);
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
      http.end();
      cb.st(STATUS_HTTPFAIL, PSTR("Không thể mở yêu cầu HTTPS"));
      return false;
    }
    size = http.getSize();
    strlcpy(saveURL, url, sizeof(saveURL));
    return true;
  }

  uint32_t read(void *data, uint32_t len) override {
    return readInternal(data, len, false);
  }

  uint32_t readNonBlock(void *data, uint32_t len) override {
    return readInternal(data, len, true);
  }

  bool seek(int32_t pos, int dir) override {
    (void)pos;
    (void)dir;
    return false;
  }

  bool close() override {
    http.end();
    return true;
  }

  bool isOpen() override {
    return http.connected();
  }

  uint32_t getSize() override {
    return size;
  }

  uint32_t getPos() override {
    return pos;
  }

 private:
  enum { STATUS_HTTPFAIL = 2, STATUS_DISCONNECTED, STATUS_NODATA };

  uint32_t readInternal(void *data, uint32_t len, bool nonBlock) {
    if (!data || !len) return 0;
    if (!http.connected()) {
      cb.st(STATUS_DISCONNECTED, PSTR("Luồng HTTPS bị ngắt kết nối"));
      return 0;
    }
    if ((size > 0) && (pos >= size)) return 0;
    WiFiClient *stream = http.getStreamPtr();
    if (!stream) return 0;
    if ((size > 0) && (len > (uint32_t)(size - pos))) len = size - pos;
    if (!nonBlock) {
      uint32_t start = millis();
      while ((stream->available() < (int)len) && (millis() - start < 500)) yield();
    }
    size_t avail = stream->available();
    if (!nonBlock && !avail) {
      cb.st(STATUS_NODATA, PSTR("Không có dữ liệu luồng HTTPS nào khả dụng."));
      return 0;
    }
    if (!avail) return 0;
    if (avail < len) len = avail;
    int bytesRead = stream->read(reinterpret_cast<uint8_t *>(data), len);
    if (bytesRead <= 0) return 0;
    pos += bytesRead;
    return bytesRead;
  }

  WiFiClientSecure client;
  HTTPClient http;
  int pos;
  int size;
  char saveURL[320];
};

static bool vbotServerStartupEnabled();

static bool micActive() {
  return vbotServerStartupEnabled() && (cfg.micEnabled || temporaryMicEnabled);
}

static void restoreTemporaryMic() {
  if (!cfg.micEnabled && temporaryMicEnabled) {
    temporaryMicEnabled = false;
    LOG("Mic tạm thời đã được khôi phục về chế độ tắt tiếng.");
  }
}

static String htmlEscape(const String &s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else out += c;
  }
  return out;
}

static const char *CONTENT_HTML = "text/html; charset=utf-8";
static const char *CONTENT_TEXT = "text/plain; charset=utf-8";
static const char *CONTENT_JSON = "application/json; charset=utf-8";

static void sendJsonResult(AsyncWebServerRequest *request, int status, bool success, const char *message) {
  JsonDocument doc;
  doc["success"] = success;
  doc["message"] = message ? message : "";
  String body;
  serializeJson(doc, body);
  request->send(status, CONTENT_JSON, body);
}

static void sendCorsJsonResult(AsyncWebServerRequest *request, int status, bool success, const char *message) {
  JsonDocument doc;
  doc["success"] = success;
  doc["message"] = message ? message : "";
  String body;
  serializeJson(doc, body);
  AsyncWebServerResponse *response = request->beginResponse(status, CONTENT_JSON, body);
  response->addHeader("Access-Control-Allow-Origin", "*");
  request->send(response);
}

static bool clientAcceptsGzip(AsyncWebServerRequest *request) {
  if (!request->hasHeader("Accept-Encoding")) return false;
  String encoding = request->getHeader("Accept-Encoding")->value();
  encoding.toLowerCase();
  return encoding.indexOf("gzip") >= 0;
}

static bool littleFsFileExists(const char *path) {
  String gzPath = String(path) + ".gz";
  return LittleFS.exists(gzPath) || LittleFS.exists(path);
}

// AsyncFileResponse tự thêm Content-Disposition, ETag và Cache-Control bằng
// nhiều node heap. Khi RAM phân mảnh, chính các header phụ này từng ném
// std::bad_alloc và làm ESP32 abort. Response này chỉ stream file cần thiết.
class LightFileResponse final : public AsyncAbstractResponse {
  File file;
public:
  LightFileResponse(File &&source, const char *contentType, bool gzip) : file(std::move(source)) {
    _code = 200;
    _contentType = contentType;
    _contentLength = file.size();
    if (gzip) addHeader("Content-Encoding", "gzip");
  }
  bool _sourceValid() const override { return (bool)file; }
  size_t _fillBuffer(uint8_t *buffer, size_t maxLen) override { return file.read(buffer, maxLen); }
};

static void sendLittleFsFile(AsyncWebServerRequest *request, const char *path, const char *contentType, bool cache = true) {
  String gzPath = String(path) + ".gz";
  bool gzExists = LittleFS.exists(gzPath);
  if (clientAcceptsGzip(request) && gzExists) {
    File file = LittleFS.open(gzPath, "r");
    auto *response = new (std::nothrow) LightFileResponse(std::move(file), contentType, true);
    if (!response) {
      request->send(503, CONTENT_TEXT, "Không đủ RAM để mở WebUI.");
      return;
    }
    request->send(response);
    return;
  }
  if (gzExists) {
    request->send(406, CONTENT_TEXT, "Trình duyệt không hỗ trợ gzip cho file WebUI này.");
    return;
  }
  if (LittleFS.exists(path)) {
    File file = LittleFS.open(path, "r");
    auto *response = new (std::nothrow) LightFileResponse(std::move(file), contentType, false);
    if (!response) {
      request->send(503, CONTENT_TEXT, "Không đủ RAM để mở WebUI.");
      return;
    }
    request->send(response);
  } else {
    request->send(404, CONTENT_TEXT, "Thiếu file WebUI trong LittleFS.");
  }
}

static String socketUrl() {
  return String("ws://") + cfg.serverHost + ":" + String(cfg.serverPort) + cfg.serverPath;
}

static String normalizeBaseUrl(const char *url) {
  String out(url ? url : "");
  out.trim();
  while (out.endsWith("/")) out.remove(out.length() - 1);
  return out;
}

static bool validStreamApiUrl(const String &url) {
  return url.startsWith("http://");
}

static String encodeQueryParam(const String &value) {
  String out;
  static const char hex[] = "0123456789ABCDEF";
  for (size_t i = 0; i < value.length(); i++) {
    uint8_t c = (uint8_t)value[i];
    bool keep = (c >= 'A' && c <= 'Z') ||
                (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.' || c == '~';
    if (keep) {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

static String streamApiBaseUrl() {
  String custom = normalizeBaseUrl(cfg.streamApiUrl);
  if (cfg.useCustomStreamApiUrl && validStreamApiUrl(custom)) return custom;
  return String("http://") + cfg.serverHost + ":" + String(cfg.apiPort);
}

static String defaultStreamApiBaseUrl() {
  return String("http://") + cfg.serverHost + ":" + String(cfg.apiPort);
}

static bool isDirectVBotServerAudioUrl(const String &url) {
  String httpHost = String("http://") + cfg.serverHost;
  bool sameHost = url.startsWith(httpHost + "/") || url.startsWith(httpHost + ":");
  if (!sameHost || url.indexOf("/audio_proxy") >= 0) return false;

  String lower = url;
  lower.toLowerCase();
  return lower.indexOf("/assets/sound/") >= 0 ||
         lower.endsWith(".mp3") || lower.indexOf(".mp3?") >= 0 ||
         lower.endsWith(".m4a") || lower.indexOf(".m4a?") >= 0 ||
         lower.endsWith(".aac") || lower.indexOf(".aac?") >= 0 ||
         lower.endsWith(".ogg") || lower.indexOf(".ogg?") >= 0 ||
          lower.endsWith(".wav") || lower.indexOf(".wav?") >= 0;
}

static bool isDirectHttpAudioFileUrl(const String &url) {
  if (!url.startsWith("http://")) return false;
  String lower = url;
  lower.toLowerCase();
  return lower.endsWith(".mp3") || lower.indexOf(".mp3?") >= 0 ||
         lower.endsWith(".m4a") || lower.indexOf(".m4a?") >= 0 ||
         lower.endsWith(".aac") || lower.indexOf(".aac?") >= 0 ||
         lower.endsWith(".ogg") || lower.indexOf(".ogg?") >= 0 ||
         lower.endsWith(".wav") || lower.indexOf(".wav?") >= 0;
}

static String effectiveAudioProxyUrl(const char *url) {
  String original(url ? url : "");
  String custom = normalizeBaseUrl(cfg.streamApiUrl);
  bool useCustomProxy = cfg.useCustomStreamApiUrl && validStreamApiUrl(custom);
  if (!useCustomProxy && original.indexOf("/audio_proxy") >= 0) return original;
  if (isDirectVBotServerAudioUrl(original)) return original;
  if (isDirectHttpAudioFileUrl(original)) return original;

  String streamApi = useCustomProxy ? custom : streamApiBaseUrl();
  if (!validStreamApiUrl(streamApi)) return original;
  String encodedOriginal = encodeQueryParam(original);
  if (streamApi.indexOf("/audio_proxy") >= 0) {
    if (streamApi.endsWith("url=")) return streamApi + encodedOriginal;
    return streamApi + (streamApi.indexOf("?") >= 0 ? "&url=" : "?url=") + encodedOriginal;
  }

  return streamApi + "/audio_proxy?url=" + encodedOriginal;
}

static String fallbackAudioProxyUrl(const char *url, const char *currentUrl) {
  if (!cfg.fallbackStreamApiToServer || !cfg.useCustomStreamApiUrl) return "";

  String original(url ? url : "");
  if (!(original.startsWith("http://") || original.startsWith("https://")) || isDirectVBotServerAudioUrl(original)) return "";

  // Nếu server đã gửi URL /audio_proxy của chính nó thì fallback phải dùng
  // nguyên URL đó. Không bọc proxy VBot bên trong một proxy VBot khác vì lớp
  // kiểm tra SSRF sẽ từ chối địa chỉ LAN của chính máy chủ.
  String defaultApi = defaultStreamApiBaseUrl();
  if (original.startsWith(defaultApi + "/audio_proxy")) {
    return original == String(currentUrl ? currentUrl : "") ? "" : original;
  }

  String fallbackApi = defaultApi;
  if (!validStreamApiUrl(fallbackApi)) return "";

  String encodedOriginal = encodeQueryParam(original);
  String fallbackUrl = fallbackApi + "/audio_proxy?url=" + encodedOriginal;
  if (fallbackUrl == String(currentUrl ? currentUrl : "")) return "";
  return fallbackUrl;
}

static String chipIdHex() {
  char id[13];
  snprintf(id, sizeof(id), "%04X%08X", (uint16_t)(ESP.getEfuseMac() >> 32), (uint32_t)ESP.getEfuseMac());
  return String(id);
}

static String configPortalSsid() {
  return "VBot_Client_AP_" + chipIdHex();
}

static bool chipIsEsp32S3() {
  String model = ESP.getChipModel();
  model.toUpperCase();
  return model.indexOf("S3") >= 0;
}

static const char *buildTarget() {
  return VBOT_BUILD_TARGET;
}

static const char *updateFamily() {
  return VBOT_UPDATE_FAMILY;
}

static const char *updateTargetDirectory() {
  static char dir[32];
  if (strcmp(updateFamily(), "ESP32S3") != 0) {
    strcpy(dir, "ESP32");
    return dir;
  }

  const char *target = buildTarget();
  if (strcmp(target, "ESP32S3") == 0 || strcmp(target, "ESP32S3_N8") == 0) {
    strcpy(dir, "ESP32S3");
  } else if (strncmp(target, "ESP32S3_", 8) == 0) {
    snprintf(dir, sizeof(dir), "%s", target);
  } else {
    strcpy(dir, "ESP32S3");
  }
  return dir;
}

static const char *updateVersionUrl() {
  static char url[256];
  snprintf(url, sizeof(url),
    "https://raw.githubusercontent.com/marion001/VBot_Client_Offline/main/SOCKET_CONNECTION/%s/Version.json",
    updateTargetDirectory());
  return url;
}

static const char *updateVersionFallbackUrl() {
  static char url[256];
  snprintf(url, sizeof(url),
    "https://github.com/marion001/VBot_Client_Offline/raw/refs/heads/main/SOCKET_CONNECTION/%s/Version.json",
    updateTargetDirectory());
  return url;
}

static const char *updateBinUrl() {
  static char url[256];
  snprintf(url, sizeof(url),
    "https://raw.githubusercontent.com/marion001/VBot_Client_Offline/main/SOCKET_CONNECTION/%s/bin/firmware.bin",
    updateTargetDirectory());
  return url;
}

static const char *updateLittleFsBinUrl() {
  static char url[256];
  snprintf(url, sizeof(url),
    "https://raw.githubusercontent.com/marion001/VBot_Client_Offline/main/SOCKET_CONNECTION/%s/bin/littlefs.bin",
    updateTargetDirectory());
  return url;
}

static String sanitizeIdentifier(const String &input) {
  String s = input;
  const char *from[] = {
    "à","á","ạ","ả","ã","â","ầ","ấ","ậ","ẩ","ẫ","ă","ằ","ắ","ặ","ẳ","ẵ",
    "è","é","ẹ","ẻ","ẽ","ê","ề","ế","ệ","ể","ễ",
    "ì","í","ị","ỉ","ĩ",
    "ò","ó","ọ","ỏ","õ","ô","ồ","ố","ộ","ổ","ỗ","ơ","ờ","ớ","ợ","ở","ỡ",
    "ù","ú","ụ","ủ","ũ","ư","ừ","ứ","ự","ử","ữ",
    "ỳ","ý","ỵ","ỷ","ỹ","đ",
    "À","Á","Ạ","Ả","Ã","Â","Ầ","Ấ","Ậ","Ẩ","Ẫ","Ă","Ằ","Ắ","Ặ","Ẳ","Ẵ",
    "È","É","Ẹ","Ẻ","Ẽ","Ê","Ề","Ế","Ệ","Ể","Ễ",
    "Ì","Í","Ị","Ỉ","Ĩ",
    "Ò","Ó","Ọ","Ỏ","Õ","Ô","Ồ","Ố","Ộ","Ổ","Ỗ","Ơ","Ờ","Ớ","Ợ","Ở","Ỡ",
    "Ù","Ú","Ụ","Ủ","Ũ","Ư","Ừ","Ứ","Ự","Ử","Ữ",
    "Ỳ","Ý","Ỵ","Ỷ","Ỹ","Đ"
  };
  const char *to[] = {
    "a","a","a","a","a","a","a","a","a","a","a","a","a","a","a","a","a",
    "e","e","e","e","e","e","e","e","e","e","e",
    "i","i","i","i","i",
    "o","o","o","o","o","o","o","o","o","o","o","o","o","o","o","o","o",
    "u","u","u","u","u","u","u","u","u","u","u",
    "y","y","y","y","y","d",
    "A","A","A","A","A","A","A","A","A","A","A","A","A","A","A","A","A",
    "E","E","E","E","E","E","E","E","E","E","E",
    "I","I","I","I","I",
    "O","O","O","O","O","O","O","O","O","O","O","O","O","O","O","O","O",
    "U","U","U","U","U","U","U","U","U","U","U",
    "Y","Y","Y","Y","Y","D"
  };
  for (size_t i = 0; i < sizeof(from) / sizeof(from[0]); i++) s.replace(from[i], to[i]);
  String out;
  bool lastUnderscore = false;
  for (size_t i = 0; i < s.length(); i++) {
    unsigned char c = (unsigned char)s[i];
    bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (keep) {
      out += (char)c;
      lastUnderscore = false;
    } else if (c == ' ' || c == '_' || c == '-') {
      if (!lastUnderscore && out.length() > 0) {
        out += '_';
        lastUnderscore = true;
      }
    }
  }
  while (out.endsWith("_")) out.remove(out.length() - 1);
  if (!out.length()) out = "VBot_Client";
  return out;
}

static String sessionPrefixFromClientName() {
  return sanitizeIdentifier(cfg.clientName);
}

static void copyPrefString(const char *key, char *dst, size_t len, const char *fallback) {
  String value = prefs.isKey(key) ? prefs.getString(key, fallback) : String(fallback);
  strlcpy(dst, value.c_str(), len);
}

static void lockCfg() {
  if (cfgMutex) xSemaphoreTake(cfgMutex, portMAX_DELAY);
}

static void unlockCfg() {
  if (cfgMutex) xSemaphoreGive(cfgMutex);
}

static bool inputModeRotary() {
  return strcmp(cfg.inputMode, "rotary") == 0;
}

static bool vbotServerStartupEnabled() {
  return strcmp(cfg.startupMode, "standalone") != 0;
}

static String validateHardwareGpioPins(
  int pinLed, int pinBtnMic, int pinBtnVolUp, int pinBtnVolDown, int pinBtnWake,
  int pinRotaryClk, int pinRotaryDt, int pinRotarySw,
  int micBclk, int micWs, int micDin, int dacBclk, int dacLrc, int dacDout
);

static void loadConfig() {
  prefs.begin("vbot", false);
  copyPrefString("cname", cfg.clientName, sizeof(cfg.clientName), cfg.clientName);
  copyPrefString("host", cfg.serverHost, sizeof(cfg.serverHost), cfg.serverHost);
  copyPrefString("path", cfg.serverPath, sizeof(cfg.serverPath), cfg.serverPath);
  copyPrefString("sapi", cfg.streamApiUrl, sizeof(cfg.streamApiUrl), cfg.streamApiUrl);
  cfg.useCustomStreamApiUrl = prefs.getBool("usesapi", cfg.useCustomStreamApiUrl);
  cfg.fallbackStreamApiToServer = prefs.getBool("sfbapi", cfg.fallbackStreamApiToServer);
  strlcpy(cfg.sessionId, sessionPrefixFromClientName().c_str(), sizeof(cfg.sessionId));
  copyPrefString("startupmode", cfg.startupMode, sizeof(cfg.startupMode), cfg.startupMode);
  copyPrefString("mode", cfg.workingMode, sizeof(cfg.workingMode), cfg.workingMode);
  cfg.serverPort = prefs.getUShort("port", cfg.serverPort);
  cfg.apiPort = prefs.getUShort("apiport", cfg.apiPort);
  cfg.apiPort = constrain(cfg.apiPort, (uint16_t)1, (uint16_t)65535);
  cfg.volume = prefs.getUChar("vol", cfg.volume);
  cfg.ledEnabled = prefs.getBool("leden", cfg.ledEnabled);
  cfg.buttonsEnabled = prefs.getBool("btnen", cfg.buttonsEnabled);
  copyPrefString("inputmode", cfg.inputMode, sizeof(cfg.inputMode), cfg.inputMode);
  cfg.ledBrightnessPct = prefs.getUChar("ledbr", cfg.ledBrightnessPct);
  cfg.ledCount = prefs.getUShort("ledcnt", cfg.ledCount);
  copyPrefString("lthink", cfg.ledThinkColor, sizeof(cfg.ledThinkColor), cfg.ledThinkColor);
  copyPrefString("lmute", cfg.ledMuteColor, sizeof(cfg.ledMuteColor), cfg.ledMuteColor);
  cfg.pinLed = prefs.getInt("pled", cfg.pinLed);
  cfg.pinBtnMic = prefs.getInt("bmic", cfg.pinBtnMic);
  cfg.pinBtnVolUp = prefs.getInt("bup", cfg.pinBtnVolUp);
  cfg.pinBtnVolDown = prefs.getInt("bdn", cfg.pinBtnVolDown);
  cfg.pinBtnWake = prefs.getInt("bwake", cfg.pinBtnWake);
  cfg.pinRotaryClk = prefs.getInt("rclk", cfg.pinRotaryClk);
  cfg.pinRotaryDt = prefs.getInt("rdt", cfg.pinRotaryDt);
  cfg.pinRotarySw = prefs.getInt("rsw", cfg.pinRotarySw);
  cfg.rotaryVolumeStep = prefs.getUChar("rstep", cfg.rotaryVolumeStep);
  cfg.micBclk = prefs.getInt("mbclk", cfg.micBclk);
  cfg.micWs = prefs.getInt("mws", cfg.micWs);
  cfg.micDin = prefs.getInt("mdin", cfg.micDin);
  copyPrefString("mch", cfg.micChannel, sizeof(cfg.micChannel), cfg.micChannel);
  cfg.dacBclk = prefs.getInt("dbclk", cfg.dacBclk);
  cfg.dacLrc = prefs.getInt("dlrc", cfg.dacLrc);
  cfg.dacDout = prefs.getInt("ddout", cfg.dacDout);
  cfg.micEnabled = prefs.getBool("micen", cfg.micEnabled);
  cfg.hotwordStreamEnabled = prefs.getBool("hword", cfg.hotwordStreamEnabled);
  cfg.conversationMode = prefs.getBool("conv", cfg.conversationMode);
  cfg.startupSoundEnabled = prefs.getBool("bootsnd", cfg.startupSoundEnabled);
  cfg.serialLogs = prefs.getBool("logs", cfg.serialLogs);
  cfg.mdnsEnabled = prefs.getBool("mdns", cfg.mdnsEnabled);
  cfg.mqttEnabled = prefs.getBool("mqen", cfg.mqttEnabled);
  copyPrefString("mqhost", cfg.mqttHost, sizeof(cfg.mqttHost), cfg.mqttHost);
  cfg.mqttPort = prefs.getUShort("mqport", cfg.mqttPort);
  copyPrefString("mquser", cfg.mqttUsername, sizeof(cfg.mqttUsername), cfg.mqttUsername);
  copyPrefString("mqpass", cfg.mqttPassword, sizeof(cfg.mqttPassword), cfg.mqttPassword);
  copyPrefString("mqclient", cfg.mqttClientName, sizeof(cfg.mqttClientName), cfg.mqttClientName);
  if (!prefs.isKey("mqclient")) {
    uint64_t chipId = ESP.getEfuseMac();
    snprintf(
      cfg.mqttClientName,
      sizeof(cfg.mqttClientName),
      "VBot_Client_ESP32_%06llX",
      (unsigned long long)(chipId & 0xFFFFFFULL)
    );
  }
  copyPrefString("wla", cfg.wakeLongAction, sizeof(cfg.wakeLongAction), cfg.wakeLongAction);
  cfg.micGain = prefs.isKey("micgain") ? prefs.getFloat("micgain", cfg.micGain) : cfg.micGain;
  cfg.pcmVolumeBoost = prefs.isKey("pcmboost") ? prefs.getFloat("pcmboost", cfg.pcmVolumeBoost) : cfg.pcmVolumeBoost;
  cfg.buttonPressDebounceMs = prefs.getUShort("bdbp", cfg.buttonPressDebounceMs);
  cfg.buttonReleaseDebounceMs = prefs.getUShort("bdbr", cfg.buttonReleaseDebounceMs);
  cfg.buttonHoldMs = prefs.getUShort("bhold", cfg.buttonHoldMs);
  cfg.volume = constrain(cfg.volume, 0, 100);
  cfg.ledBrightnessPct = constrain(cfg.ledBrightnessPct, 0, 100);
  cfg.ledCount = constrain(cfg.ledCount, (uint16_t)1, (uint16_t)300);
  cfg.micGain = constrain(cfg.micGain, 0.1f, 16.0f);
  cfg.pcmVolumeBoost = constrain(cfg.pcmVolumeBoost, 0.1f, 16.0f);
  cfg.buttonPressDebounceMs = constrain(cfg.buttonPressDebounceMs, (uint16_t)10, (uint16_t)1000);
  cfg.buttonReleaseDebounceMs = constrain(cfg.buttonReleaseDebounceMs, (uint16_t)10, (uint16_t)1000);
  cfg.buttonHoldMs = constrain(cfg.buttonHoldMs, (uint16_t)300, (uint16_t)10000);
  cfg.rotaryVolumeStep = constrain(cfg.rotaryVolumeStep, (uint8_t)1, (uint8_t)20);
  if (strcmp(cfg.inputMode, "rotary") != 0) strlcpy(cfg.inputMode, "button", sizeof(cfg.inputMode));
  if (strcmp(cfg.startupMode, "standalone") != 0) strlcpy(cfg.startupMode, "vbot_server", sizeof(cfg.startupMode));
  if (cfg.micChannel[0] != 'R' && cfg.micChannel[0] != 'r') strlcpy(cfg.micChannel, "L", sizeof(cfg.micChannel));
  else strlcpy(cfg.micChannel, "R", sizeof(cfg.micChannel));
  const char *wakeActions[] = {"restart", "clean_all", "reset_wifi", "reset_nvs", "stop_audio"};
  bool validWakeAction = false;
  for (const char *action : wakeActions) {
    if (!strcmp(cfg.wakeLongAction, action)) validWakeAction = true;
  }
  if (!validWakeAction) strlcpy(cfg.wakeLongAction, "stop_audio", sizeof(cfg.wakeLongAction));
}

static void saveConfig(const AppConfig &saved = cfg) {
  const AppConfig &cfg = saved;
  lockNvs();
  prefs.putString("cname", cfg.clientName);
  prefs.putString("host", cfg.serverHost);
  prefs.putUShort("port", cfg.serverPort);
  prefs.putUShort("apiport", cfg.apiPort);
  prefs.putString("path", cfg.serverPath);
  prefs.putString("sapi", cfg.streamApiUrl);
  prefs.putBool("usesapi", cfg.useCustomStreamApiUrl);
  prefs.putBool("sfbapi", cfg.fallbackStreamApiToServer);
  prefs.putString("sid", sanitizeIdentifier(String(cfg.clientName)));
  prefs.putString("startupmode", cfg.startupMode);
  prefs.putString("mode", cfg.workingMode);
  prefs.putUChar("vol", cfg.volume);
  prefs.putBool("leden", cfg.ledEnabled);
  prefs.putBool("btnen", cfg.buttonsEnabled);
  prefs.putString("inputmode", cfg.inputMode);
  prefs.putUChar("ledbr", cfg.ledBrightnessPct);
  prefs.putUShort("ledcnt", cfg.ledCount);
  prefs.putString("lthink", cfg.ledThinkColor);
  prefs.putString("lmute", cfg.ledMuteColor);
  prefs.putInt("pled", cfg.pinLed);
  prefs.putInt("bmic", cfg.pinBtnMic);
  prefs.putInt("bup", cfg.pinBtnVolUp);
  prefs.putInt("bdn", cfg.pinBtnVolDown);
  prefs.putInt("bwake", cfg.pinBtnWake);
  prefs.putInt("rclk", cfg.pinRotaryClk);
  prefs.putInt("rdt", cfg.pinRotaryDt);
  prefs.putInt("rsw", cfg.pinRotarySw);
  prefs.putUChar("rstep", cfg.rotaryVolumeStep);
  prefs.putInt("mbclk", cfg.micBclk);
  prefs.putInt("mws", cfg.micWs);
  prefs.putInt("mdin", cfg.micDin);
  prefs.putString("mch", cfg.micChannel);
  prefs.putInt("dbclk", cfg.dacBclk);
  prefs.putInt("dlrc", cfg.dacLrc);
  prefs.putInt("ddout", cfg.dacDout);
  prefs.putBool("micen", cfg.micEnabled);
  prefs.putBool("hword", cfg.hotwordStreamEnabled);
  prefs.putBool("conv", cfg.conversationMode);
  prefs.putBool("bootsnd", cfg.startupSoundEnabled);
  prefs.putBool("logs", cfg.serialLogs);
  prefs.putBool("mdns", cfg.mdnsEnabled);
  prefs.putBool("mqen", cfg.mqttEnabled);
  prefs.putString("mqhost", cfg.mqttHost);
  prefs.putUShort("mqport", cfg.mqttPort);
  prefs.putString("mquser", cfg.mqttUsername);
  prefs.putString("mqpass", cfg.mqttPassword);
  prefs.putString("mqclient", cfg.mqttClientName);
  prefs.putString("wla", cfg.wakeLongAction);
  prefs.putFloat("micgain", cfg.micGain);
  prefs.putFloat("pcmboost", cfg.pcmVolumeBoost);
  prefs.putUShort("bdbp", cfg.buttonPressDebounceMs);
  prefs.putUShort("bdbr", cfg.buttonReleaseDebounceMs);
  prefs.putUShort("bhold", cfg.buttonHoldMs);
  unlockNvs();
}

static void migrateUnsafeDefaultPins() {
  bool changed = false;
  if (!prefs.getBool("pin_safe_v2", false)) {
#if !defined(ESP32S3) && !defined(CONFIG_IDF_TARGET_ESP32S3)
    if (cfg.pinLed == 4) {
      cfg.pinLed = 23;
      changed = true;
    }
    if (cfg.micWs == 15) {
      cfg.micWs = 13;
      changed = true;
    }
    if (cfg.dacLrc == 12) {
      cfg.dacLrc = 26;
      changed = true;
    }
    if (cfg.dacDout == 13) {
      cfg.dacDout = 25;
      changed = true;
    }
    if (cfg.pinBtnVolDown == 25) {
      cfg.pinBtnVolDown = 18;
      changed = true;
    }
    if (cfg.pinBtnWake == 26) {
      cfg.pinBtnWake = 19;
      changed = true;
    }
#endif
    prefs.putBool("pin_safe_v2", true);
  }

#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
  // ESP32-S3 N8R8/N16R8 dùng GPIO26..37 cho flash/OPI PSRAM. Không cấu hình
  // pinMode hay I2S trên dải này. GPIO22..25 cũng không tồn tại trên S3.
  if (!prefs.getBool("pin_safe_s3_v2", false)) {
    if (cfg.pinLed >= 22 && cfg.pinLed <= 25) {
      cfg.pinLed = 4;
      changed = true;
    }
    if (cfg.pinBtnMic >= 26 && cfg.pinBtnMic <= 37) { cfg.pinBtnMic = 5; changed = true; }
    if (cfg.pinBtnVolUp >= 26 && cfg.pinBtnVolUp <= 37) { cfg.pinBtnVolUp = 6; changed = true; }
    if (cfg.pinBtnVolDown >= 26 && cfg.pinBtnVolDown <= 37) { cfg.pinBtnVolDown = 7; changed = true; }
    if (cfg.pinBtnWake >= 26 && cfg.pinBtnWake <= 37) { cfg.pinBtnWake = 8; changed = true; }
    if (cfg.pinRotaryClk >= 26 && cfg.pinRotaryClk <= 37) { cfg.pinRotaryClk = 5; changed = true; }
    if (cfg.pinRotaryDt >= 26 && cfg.pinRotaryDt <= 37) { cfg.pinRotaryDt = 6; changed = true; }
    if (cfg.pinRotarySw >= 26 && cfg.pinRotarySw <= 37) { cfg.pinRotarySw = 8; changed = true; }
    if (cfg.micBclk >= 26 && cfg.micBclk <= 37) { cfg.micBclk = 14; changed = true; }
    if (cfg.micWs >= 26 && cfg.micWs <= 37) { cfg.micWs = 13; changed = true; }
    if (cfg.micDin >= 26 && cfg.micDin <= 37) { cfg.micDin = 12; changed = true; }
    if (cfg.dacBclk >= 26 && cfg.dacBclk <= 37) { cfg.dacBclk = 17; changed = true; }
    if (cfg.dacLrc >= 26 && cfg.dacLrc <= 37) { cfg.dacLrc = 18; changed = true; }
    if ((cfg.dacDout >= 22 && cfg.dacDout <= 25) || (cfg.dacDout >= 26 && cfg.dacDout <= 37)) {
      cfg.dacDout = 21;
      changed = true;
    }
    prefs.putBool("pin_safe_s3_v2", true);
  }

  // Chỉ chuyển các bộ chân mặc định S3 cũ đã biết. Không ghi đè cấu hình GPIO
  // tùy chỉnh của người dùng chỉ vì thiết bị chưa có cờ migration.
  if (!prefs.getBool("pin_def_s3_v3", false)) {
    if (cfg.pinLed == 4) {
      cfg.pinLed = 48;
      changed = true;
    }
    bool oldButtons =
      (cfg.pinBtnMic == 5 && cfg.pinBtnVolUp == 6 && cfg.pinBtnVolDown == 7 && cfg.pinBtnWake == 8) ||
      (cfg.pinBtnMic == 5 && cfg.pinBtnVolUp == 6 && cfg.pinBtnVolDown == 18 && cfg.pinBtnWake == 19);
    if (oldButtons) {
      cfg.pinBtnMic = 8;
      cfg.pinBtnVolUp = 9;
      cfg.pinBtnVolDown = 10;
      cfg.pinBtnWake = 11;
      changed = true;
    }
    bool oldRotary =
      (cfg.pinRotaryClk == 5 && cfg.pinRotaryDt == 6 && cfg.pinRotarySw == 8) ||
      (cfg.pinRotaryClk == 5 && cfg.pinRotaryDt == 6 && cfg.pinRotarySw == 19);
    if (oldRotary) {
      cfg.pinRotaryClk = 8;
      cfg.pinRotaryDt = 9;
      cfg.pinRotarySw = 11;
      changed = true;
    }
    if (cfg.micBclk == 14 && cfg.micWs == 13 && cfg.micDin == 12) {
      cfg.micBclk = 5;
      cfg.micWs = 4;
      cfg.micDin = 6;
      changed = true;
    }
    if (cfg.dacBclk == 17 && cfg.dacLrc == 18 && cfg.dacDout == 21) {
      cfg.dacBclk = 15;
      cfg.dacLrc = 16;
      cfg.dacDout = 7;
      changed = true;
    }
    prefs.putBool("pin_def_s3_v3", true);
  }

  // Bộ nút mặc định ESP32-S3 v4: tránh GPIO0 boot strap và dải GPIO của
  // flash/OPI PSRAM trên các module N8R8/N16R8.
  if (!prefs.getBool("btn_def_s3_v4", false)) {
    if (cfg.pinBtnMic == 8 && cfg.pinBtnVolUp == 9 && cfg.pinBtnVolDown == 10 && cfg.pinBtnWake == 11) {
      cfg.pinBtnMic = 41;
      cfg.pinBtnVolUp = 40;
      cfg.pinBtnVolDown = 39;
      cfg.pinBtnWake = 42;
      changed = true;
    }
    if (cfg.pinRotaryClk == 8 && cfg.pinRotaryDt == 9 && cfg.pinRotarySw == 11) {
      cfg.pinRotaryClk = 41;
      cfg.pinRotaryDt = 40;
      cfg.pinRotarySw = 42;
      changed = true;
    }
    prefs.putBool("btn_def_s3_v4", true);
  }
#endif

  if (changed) {
    saveConfig();
    LOG("Đã chuyển các chân GPIO mặc định không hợp lệ sang cấu hình an toàn cho chip hiện tại.");
  }
}

static void fillConfigJson(JsonObject doc) {
  doc["version"] = VBOT_CLIENT_VERSION;
  doc["client"]["name"] = cfg.clientName;
  doc["server"]["host"] = cfg.serverHost;
  doc["server"]["port"] = cfg.serverPort;
  doc["server"]["api_port"] = cfg.apiPort;
  doc["server"]["path"] = cfg.serverPath;
  doc["server"]["socket"] = socketUrl();
  doc["server"]["stream_api_url"] = cfg.streamApiUrl;
  doc["server"]["use_custom_stream_api_url"] = cfg.useCustomStreamApiUrl;
  doc["server"]["fallback_stream_api_to_server"] = cfg.fallbackStreamApiToServer;
  doc["server"]["effective_stream_api_url"] = streamApiBaseUrl();
  doc["client"]["session_prefix"] = sessionPrefixFromClientName();
  doc["client"]["startup_mode"] = cfg.startupMode;
  doc["client"]["working_mode"] = cfg.workingMode;
  doc["audio"]["volume"] = cfg.volume;
  doc["audio"]["mic_enabled"] = cfg.micEnabled;
  doc["audio"]["hotword_stream_enabled"] = cfg.hotwordStreamEnabled;
  doc["audio"]["conversation_mode"] = cfg.conversationMode;
  doc["audio"]["startup_sound_enabled"] = cfg.startupSoundEnabled;
  doc["audio"]["mic_gain"] = cfg.micGain;
  doc["audio"]["pcm_volume_boost"] = cfg.pcmVolumeBoost;
  doc["logs"]["serial"] = cfg.serialLogs;
  doc["network"]["mdns_enabled"] = cfg.mdnsEnabled;
  doc["mqtt"]["active"] = cfg.mqttEnabled;
  doc["mqtt"]["host"] = cfg.mqttHost;
  doc["mqtt"]["port"] = cfg.mqttPort;
  doc["mqtt"]["username"] = cfg.mqttUsername;
  doc["mqtt"]["password"] = cfg.mqttPassword;
  doc["mqtt"]["client_name"] = cfg.mqttClientName;
  doc["led"]["active"] = cfg.ledEnabled;
  doc["led"]["gpio"] = cfg.pinLed;
  doc["led"]["count"] = cfg.ledCount;
  doc["led"]["brightness_percent"] = cfg.ledBrightnessPct;
  doc["led"]["think_color"] = cfg.ledThinkColor;
  doc["led"]["mute_color"] = cfg.ledMuteColor;
  doc["button"]["active"] = cfg.buttonsEnabled;
  doc["button"]["input_mode"] = cfg.inputMode;
  doc["button"]["mic"] = cfg.pinBtnMic;
  doc["button"]["volume_up"] = cfg.pinBtnVolUp;
  doc["button"]["volume_down"] = cfg.pinBtnVolDown;
  doc["button"]["wakeup"] = cfg.pinBtnWake;
  doc["button"]["wakeup_long_action"] = cfg.wakeLongAction;
  doc["button"]["press_debounce_ms"] = cfg.buttonPressDebounceMs;
  doc["button"]["release_debounce_ms"] = cfg.buttonReleaseDebounceMs;
  doc["button"]["hold_ms"] = cfg.buttonHoldMs;
  doc["rotary"]["clk"] = cfg.pinRotaryClk;
  doc["rotary"]["dt"] = cfg.pinRotaryDt;
  doc["rotary"]["sw"] = cfg.pinRotarySw;
  doc["rotary"]["volume_step"] = cfg.rotaryVolumeStep;
  doc["i2s_mic"]["bclk"] = cfg.micBclk;
  doc["i2s_mic"]["ws"] = cfg.micWs;
  doc["i2s_mic"]["din"] = cfg.micDin;
  doc["i2s_mic"]["channel"] = cfg.micChannel;
  doc["i2s_dac"]["bclk"] = cfg.dacBclk;
  doc["i2s_dac"]["lrc"] = cfg.dacLrc;
  doc["i2s_dac"]["dout"] = cfg.dacDout;
}

static bool applyConfigJson(JsonDocument &doc) {
  // Dựng cấu hình trong vùng staging. Các task tiếp tục đọc ::cfg ổn định;
  // cấu hình mới chỉ được nạp từ NVS sau khi thiết bị restart.
  AppConfig pendingCfg = ::cfg;
  AppConfig &cfg = pendingCfg;
  if (doc["client"]["name"].is<const char *>()) strlcpy(cfg.clientName, doc["client"]["name"], sizeof(cfg.clientName));
  if (doc["server"]["host"].is<const char *>()) strlcpy(cfg.serverHost, doc["server"]["host"], sizeof(cfg.serverHost));
  if (doc["server"]["port"].is<int>()) cfg.serverPort = constrain((int)doc["server"]["port"], 1, 65535);
  if (doc["server"]["api_port"].is<int>()) cfg.apiPort = constrain((int)doc["server"]["api_port"], 1, 65535);
  if (doc["server"]["path"].is<const char *>()) strlcpy(cfg.serverPath, doc["server"]["path"], sizeof(cfg.serverPath));
  if (doc["server"]["stream_api_url"].is<const char *>()) strlcpy(cfg.streamApiUrl, doc["server"]["stream_api_url"], sizeof(cfg.streamApiUrl));
  if (doc["server"]["use_custom_stream_api_url"].is<bool>()) cfg.useCustomStreamApiUrl = doc["server"]["use_custom_stream_api_url"];
  if (doc["server"]["fallback_stream_api_to_server"].is<bool>()) cfg.fallbackStreamApiToServer = doc["server"]["fallback_stream_api_to_server"];
  if (doc["client"]["startup_mode"].is<const char *>()) {
    const char *startupMode = doc["client"]["startup_mode"];
    strlcpy(cfg.startupMode, startupMode && !strcmp(startupMode, "standalone") ? "standalone" : "vbot_server", sizeof(cfg.startupMode));
  }
  if (doc["client"]["working_mode"].is<const char *>()) strlcpy(cfg.workingMode, doc["client"]["working_mode"], sizeof(cfg.workingMode));
  if (doc["audio"]["volume"].is<int>()) cfg.volume = constrain((int)doc["audio"]["volume"], 0, 100);
  if (doc["audio"]["mic_enabled"].is<bool>()) cfg.micEnabled = doc["audio"]["mic_enabled"];
  if (doc["audio"]["hotword_stream_enabled"].is<bool>()) cfg.hotwordStreamEnabled = doc["audio"]["hotword_stream_enabled"];
  if (doc["audio"]["conversation_mode"].is<bool>()) cfg.conversationMode = doc["audio"]["conversation_mode"];
  if (doc["audio"]["startup_sound_enabled"].is<bool>()) cfg.startupSoundEnabled = doc["audio"]["startup_sound_enabled"];
  if (doc["audio"]["mic_gain"].is<float>()) cfg.micGain = constrain((float)doc["audio"]["mic_gain"], 0.1f, 16.0f);
  if (doc["audio"]["pcm_volume_boost"].is<float>()) cfg.pcmVolumeBoost = constrain((float)doc["audio"]["pcm_volume_boost"], 0.1f, 16.0f);
  if (doc["logs"]["serial"].is<bool>()) cfg.serialLogs = doc["logs"]["serial"];
  if (doc["network"]["mdns_enabled"].is<bool>()) cfg.mdnsEnabled = doc["network"]["mdns_enabled"];
  if (doc["mqtt"]["active"].is<bool>()) cfg.mqttEnabled = doc["mqtt"]["active"];
  if (doc["mqtt"]["host"].is<const char *>()) strlcpy(cfg.mqttHost, doc["mqtt"]["host"], sizeof(cfg.mqttHost));
  if (doc["mqtt"]["port"].is<int>()) cfg.mqttPort = constrain((int)doc["mqtt"]["port"], 1, 65535);
  if (doc["mqtt"]["username"].is<const char *>()) strlcpy(cfg.mqttUsername, doc["mqtt"]["username"], sizeof(cfg.mqttUsername));
  if (doc["mqtt"]["password"].is<const char *>()) strlcpy(cfg.mqttPassword, doc["mqtt"]["password"], sizeof(cfg.mqttPassword));
  if (doc["mqtt"]["client_name"].is<const char *>()) strlcpy(cfg.mqttClientName, doc["mqtt"]["client_name"], sizeof(cfg.mqttClientName));
  if (doc["led"]["active"].is<bool>()) cfg.ledEnabled = doc["led"]["active"];
  if (doc["led"]["gpio"].is<int>()) cfg.pinLed = doc["led"]["gpio"];
  if (doc["led"]["count"].is<int>()) cfg.ledCount = constrain((int)doc["led"]["count"], 1, 300);
  if (doc["led"]["brightness_percent"].is<int>()) cfg.ledBrightnessPct = constrain((int)doc["led"]["brightness_percent"], 0, 100);
  if (doc["led"]["think_color"].is<const char *>()) strlcpy(cfg.ledThinkColor, doc["led"]["think_color"], sizeof(cfg.ledThinkColor));
  if (doc["led"]["mute_color"].is<const char *>()) strlcpy(cfg.ledMuteColor, doc["led"]["mute_color"], sizeof(cfg.ledMuteColor));
  if (doc["button"]["active"].is<bool>()) cfg.buttonsEnabled = doc["button"]["active"];
  if (doc["button"]["input_mode"].is<const char *>()) {
    const char *mode = doc["button"]["input_mode"];
    strlcpy(cfg.inputMode, mode && !strcmp(mode, "rotary") ? "rotary" : "button", sizeof(cfg.inputMode));
  }
  if (doc["button"]["mic"].is<int>()) cfg.pinBtnMic = doc["button"]["mic"];
  if (doc["button"]["volume_up"].is<int>()) cfg.pinBtnVolUp = doc["button"]["volume_up"];
  if (doc["button"]["volume_down"].is<int>()) cfg.pinBtnVolDown = doc["button"]["volume_down"];
  if (doc["button"]["wakeup"].is<int>()) cfg.pinBtnWake = doc["button"]["wakeup"];
  if (doc["button"]["wakeup_long_action"].is<const char *>()) strlcpy(cfg.wakeLongAction, doc["button"]["wakeup_long_action"], sizeof(cfg.wakeLongAction));
  if (doc["button"]["press_debounce_ms"].is<int>()) cfg.buttonPressDebounceMs = constrain((int)doc["button"]["press_debounce_ms"], 10, 1000);
  if (doc["button"]["release_debounce_ms"].is<int>()) cfg.buttonReleaseDebounceMs = constrain((int)doc["button"]["release_debounce_ms"], 10, 1000);
  if (doc["button"]["hold_ms"].is<int>()) cfg.buttonHoldMs = constrain((int)doc["button"]["hold_ms"], 300, 10000);
  if (doc["rotary"]["clk"].is<int>()) cfg.pinRotaryClk = doc["rotary"]["clk"];
  if (doc["rotary"]["dt"].is<int>()) cfg.pinRotaryDt = doc["rotary"]["dt"];
  if (doc["rotary"]["sw"].is<int>()) cfg.pinRotarySw = doc["rotary"]["sw"];
  if (doc["rotary"]["volume_step"].is<int>()) cfg.rotaryVolumeStep = constrain((int)doc["rotary"]["volume_step"], 1, 20);
  if (doc["i2s_mic"]["bclk"].is<int>()) cfg.micBclk = doc["i2s_mic"]["bclk"];
  if (doc["i2s_mic"]["ws"].is<int>()) cfg.micWs = doc["i2s_mic"]["ws"];
  if (doc["i2s_mic"]["din"].is<int>()) cfg.micDin = doc["i2s_mic"]["din"];
  if (doc["i2s_mic"]["channel"].is<const char *>()) {
    const char *channel = doc["i2s_mic"]["channel"];
    strlcpy(cfg.micChannel, (channel && (channel[0] == 'R' || channel[0] == 'r')) ? "R" : "L", sizeof(cfg.micChannel));
  }
  if (doc["i2s_dac"]["bclk"].is<int>()) cfg.dacBclk = doc["i2s_dac"]["bclk"];
  if (doc["i2s_dac"]["lrc"].is<int>()) cfg.dacLrc = doc["i2s_dac"]["lrc"];
  if (doc["i2s_dac"]["dout"].is<int>()) cfg.dacDout = doc["i2s_dac"]["dout"];
  String gpioConflicts = validateHardwareGpioPins(
    cfg.ledEnabled ? cfg.pinLed : -1,
    cfg.buttonsEnabled && strcmp(cfg.inputMode, "rotary") != 0 ? cfg.pinBtnMic : -1,
    cfg.buttonsEnabled && strcmp(cfg.inputMode, "rotary") != 0 ? cfg.pinBtnVolUp : -1,
    cfg.buttonsEnabled && strcmp(cfg.inputMode, "rotary") != 0 ? cfg.pinBtnVolDown : -1,
    cfg.buttonsEnabled && strcmp(cfg.inputMode, "rotary") != 0 ? cfg.pinBtnWake : -1,
    cfg.buttonsEnabled && strcmp(cfg.inputMode, "rotary") == 0 ? cfg.pinRotaryClk : -1,
    cfg.buttonsEnabled && strcmp(cfg.inputMode, "rotary") == 0 ? cfg.pinRotaryDt : -1,
    cfg.buttonsEnabled && strcmp(cfg.inputMode, "rotary") == 0 ? cfg.pinRotarySw : -1,
    strcmp(cfg.startupMode, "standalone") != 0 ? cfg.micBclk : -1,
    strcmp(cfg.startupMode, "standalone") != 0 ? cfg.micWs : -1,
    strcmp(cfg.startupMode, "standalone") != 0 ? cfg.micDin : -1,
    cfg.dacBclk, cfg.dacLrc, cfg.dacDout
  );
  if (gpioConflicts.length()) {
    LOGF("Không khôi phục Config vì GPIO không hợp lệ:\n%s", gpioConflicts.c_str());
    return false;
  }
  saveConfig(pendingCfg);
  return true;
}

static bool resetAllNvs() {
  if (otaUpdateActive || fsMaintenanceActive) {
    UART_LOG("Từ chối factory reset vì OTA/LittleFS đang được cập nhật.");
    return false;
  }
  lockNvs();
  bool nvsOk = prefs.clear();
  unlockNvs();
  WiFiManager wm;
  wm.resetSettings();
  bool wifiOk = !wm.getWiFiIsSaved();
  UART_LOGF("Factory reset: xóa NVS=%s, xóa WiFi=%s", nvsOk ? "thành công" : "THẤT BẠI", wifiOk ? "thành công" : "THẤT BẠI");
  return nvsOk && wifiOk;
}

static bool resetNvsOnly() {
  if (otaUpdateActive || fsMaintenanceActive) {
    UART_LOG("Từ chối reset NVS vì OTA/LittleFS đang được cập nhật.");
    return false;
  }
  lockNvs();
  bool ok = prefs.clear();
  unlockNvs();
  UART_LOGF("Reset NVS: %s", ok ? "thành công" : "THẤT BẠI");
  return ok;
}

static void setLedEffect(LedEffect effect) {
  if (effect == ledEffect && effect != LED_VOLUME) return;
  ledEffect = effect;
}

static bool ledEffectFromString(const String &name, LedEffect &effect) {
  String value = name;
  value.trim();
  value.toUpperCase();
  if (value == "LED_SPEAK" || value == "SPEAK") effect = LED_SPEAK;
  else if (value == "LED_THINK" || value == "THINK" || value == "LED_WAKEUP" || value == "WAKEUP") effect = LED_THINK;
  else if (value == "LED_LOADING" || value == "LOADING") effect = LED_LOADING;
  else if (value == "LED_MUTE" || value == "MUTE") effect = LED_MUTE;
  else if (value == "LED_ERROR" || value == "ERROR") effect = LED_ERROR;
  else if (value == "LED_SOCKET_ERROR" || value == "SOCKET_ERROR") effect = LED_SOCKET_ERROR;
  else if (value == "LED_STARTUP" || value == "STARTUP") effect = LED_STARTUP;
  else if (value == "LED_PAUSE" || value == "PAUSE") effect = LED_PAUSE;
  else if (value == "LED_VOLUME" || value == "VOLUME") effect = LED_VOLUME;
  else if (value == "LED_OFF" || value == "OFF") effect = LED_OFF;
  else return false;
  return true;
}

static uint8_t ledBrightness() {
  return map(constrain(cfg.ledBrightnessPct, 0, 100), 0, 100, 0, 255);
}

static CRGB parseHexColor(const char *hex, const CRGB &fallback) {
  if (!hex) return fallback;
  const char *p = hex[0] == '#' ? hex + 1 : hex;
  if (strlen(p) != 6) return fallback;
  char *end = nullptr;
  uint32_t value = strtoul(p, &end, 16);
  if (!end || *end != '\0') return fallback;
  return CRGB((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
}

static void normalizeHexColor(char *dst, size_t len, const char *input, const char *fallback) {
  if (!input || !*input) {
    strlcpy(dst, fallback, len);
    return;
  }
  char tmp[8] = "#000000";
  const char *p = input[0] == '#' ? input + 1 : input;
  if (strlen(p) != 6) {
    strlcpy(dst, fallback, len);
    return;
  }
  for (uint8_t i = 0; i < 6; i++) {
    char c = p[i];
    if (!isxdigit((unsigned char)c)) {
      strlcpy(dst, fallback, len);
      return;
    }
    tmp[i + 1] = (char)toupper((unsigned char)c);
  }
  tmp[7] = '\0';
  strlcpy(dst, tmp, len);
}

static bool gpioSupportsFastLed(int pin) {
  switch (pin) {
    case 0: case 2: case 4: case 5:
    case 12: case 13: case 14: case 15: case 16: case 17: case 18: case 19:
    case 21: case 33:
      return true;
    default:
      break;
  }
  if (chipIsEsp32S3()) return pin == 48;
  return pin == 22 || pin == 23 || pin == 25 || pin == 27 || pin == 32;
}

static void initLed() {
  if (leds) {
    FastLED.clear(true);
    delete[] leds;
    leds = nullptr;
  }
  if (!cfg.ledEnabled) return;
  leds = new (std::nothrow) CRGB[cfg.ledCount];
  if (!leds) {
    LOG("Không đủ RAM để khởi tạo LED buffer");
    return;
  }
  switch (cfg.pinLed) {
    case 0: FastLED.addLeds<WS2812B, 0, GRB>(leds, cfg.ledCount); break;
    case 2: FastLED.addLeds<WS2812B, 2, GRB>(leds, cfg.ledCount); break;
    case 4: FastLED.addLeds<WS2812B, 4, GRB>(leds, cfg.ledCount); break;
    case 5: FastLED.addLeds<WS2812B, 5, GRB>(leds, cfg.ledCount); break;
    case 12: FastLED.addLeds<WS2812B, 12, GRB>(leds, cfg.ledCount); break;
    case 13: FastLED.addLeds<WS2812B, 13, GRB>(leds, cfg.ledCount); break;
    case 14: FastLED.addLeds<WS2812B, 14, GRB>(leds, cfg.ledCount); break;
    case 15: FastLED.addLeds<WS2812B, 15, GRB>(leds, cfg.ledCount); break;
    case 16: FastLED.addLeds<WS2812B, 16, GRB>(leds, cfg.ledCount); break;
    case 17: FastLED.addLeds<WS2812B, 17, GRB>(leds, cfg.ledCount); break;
    case 18: FastLED.addLeds<WS2812B, 18, GRB>(leds, cfg.ledCount); break;
    case 19: FastLED.addLeds<WS2812B, 19, GRB>(leds, cfg.ledCount); break;
    case 21: FastLED.addLeds<WS2812B, 21, GRB>(leds, cfg.ledCount); break;
#if !defined(ESP32S3) && !defined(CONFIG_IDF_TARGET_ESP32S3)
    case 22: FastLED.addLeds<WS2812B, 22, GRB>(leds, cfg.ledCount); break;
    case 23: FastLED.addLeds<WS2812B, 23, GRB>(leds, cfg.ledCount); break;
    case 25: FastLED.addLeds<WS2812B, 25, GRB>(leds, cfg.ledCount); break;
    case 27: FastLED.addLeds<WS2812B, 27, GRB>(leds, cfg.ledCount); break;
    case 32: FastLED.addLeds<WS2812B, 32, GRB>(leds, cfg.ledCount); break;
#endif
    case 33: FastLED.addLeds<WS2812B, 33, GRB>(leds, cfg.ledCount); break;
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    case 48: FastLED.addLeds<WS2812B, 48, GRB>(leds, cfg.ledCount); break;
#endif
    default:
      LOGF("FastLED không hỗ trợ GPIO %d trong firmware này; LED đã được tắt để tránh dùng nhầm GPIO", cfg.pinLed);
      delete[] leds;
      leds = nullptr;
      return;
  }
  FastLED.setBrightness(ledBrightness());
  FastLED.clear(true);
  setLedEffect(LED_STARTUP);
}

static void showSolid(const CRGB &c) {
  if (!leds) return;
  FastLED.setBrightness(ledBrightness());
  fill_solid(leds, cfg.ledCount, c);
  FastLED.show();
}

static void showRainbow(uint16_t step, uint8_t speed = 1) {
  if (!leds) return;
  FastLED.setBrightness(ledBrightness());
  fill_rainbow(leds, cfg.ledCount, step * speed, 255 / max((uint16_t)1, cfg.ledCount));
  FastLED.show();
}

static void showMuteBreath(uint16_t step) {
  if (!leds) return;
  uint8_t maxBrightness = ledBrightness();
  uint8_t minBrightness = max((uint8_t)1, (uint8_t)(maxBrightness / 10));
  uint8_t brightness = beatsin8(24, minBrightness, maxBrightness, 0, 0);
  FastLED.setBrightness(brightness);
  fill_solid(leds, cfg.ledCount, parseHexColor(cfg.ledMuteColor, CRGB(255, 140, 0)));
  FastLED.show();
}

static void showSocketErrorBreath(uint16_t step) {
  if (!leds) return;
  uint8_t maxBrightness = max((uint8_t)4, (uint8_t)(ledBrightness() / 3));
  uint8_t minBrightness = max((uint8_t)1, (uint8_t)(maxBrightness / 6));
  uint8_t brightness = beatsin8(10, minBrightness, maxBrightness, 0, 0);
  FastLED.setBrightness(brightness);
  fill_solid(leds, cfg.ledCount, CRGB(255, 40, 20));
  FastLED.show();
}

static void showStartupEffect(uint16_t step) {
  if (!leds || cfg.ledCount == 0) return;
  uint8_t baseBrightness = ledBrightness();
  uint8_t brightness = beatsin8(6, (uint8_t)max((int)48, (int)(baseBrightness / 2)), baseBrightness, 0, 0);
  FastLED.setBrightness(brightness);
  fill_solid(leds, cfg.ledCount, CRGB(8, 4, 16));
  uint16_t head = step % cfg.ledCount;
  const uint8_t trailLength = min<uint16_t>(cfg.ledCount, 10);
  uint8_t divisor = trailLength > 1 ? trailLength - 1 : 1;
  for (uint8_t tail = 0; tail < trailLength; tail++) {
    uint16_t pos = (head + cfg.ledCount - tail) % cfg.ledCount;
    uint8_t fade = 255 - (uint8_t)(tail * (200 / divisor));
    uint8_t hue = 160 + (tail * 8);
    leds[pos] = CHSV(hue, 255, fade);
  }
  FastLED.show();
}

static void showVolumeLevel(uint8_t volume) {
  if (!leds) return;
  FastLED.setBrightness(ledBrightness());
  fill_solid(leds, cfg.ledCount, CRGB::Black);
  uint16_t ledsOn = 0;
  if (volume > 0 && cfg.ledCount > 0) {
    ledsOn = ((uint32_t)volume * cfg.ledCount + 99) / 100;
    if (ledsOn > cfg.ledCount) ledsOn = cfg.ledCount;
  }
  for (uint16_t i = 0; i < ledsOn; i++) {
    leds[i] = CRGB(180, 255, 0);
  }
  FastLED.show();
}

static void showVolumeOverlay(uint8_t volume) {
  volumeLedValue = constrain(volume, 0, 100);
  volumeZeroLedActive = volumeLedValue == 0;
  volumeLedUntilMs = millis() + (volumeZeroLedActive ? VOLUME_ZERO_LED_DURATION_MS : VOLUME_LED_DURATION_MS);
  volumeLedActive = true;
}

static void ledTask(void *) {
  uint16_t step = 0;
  LedEffect lastStaticEffect = (LedEffect)255;
  for (;;) {
    if (!leds) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (ledRefreshRequested) {
      ledRefreshRequested = false;
      lastStaticEffect = (LedEffect)255;
    }
    if (volumeLedActive) {
      if ((int32_t)(millis() - volumeLedUntilMs) < 0) {
        if (volumeZeroLedActive) showSolid(CRGB::Red);
        else showVolumeLevel(volumeLedValue);
        vTaskDelay(pdMS_TO_TICKS(45));
        continue;
      }
      if (volumeZeroLedActive) showSolid(CRGB::Black);
      volumeLedActive = false;
      volumeZeroLedActive = false;
      lastStaticEffect = (LedEffect)255;
    }
    LedEffect effect = ledEffect;
    bool animated = effect == LED_SPEAK || effect == LED_LOADING || effect == LED_MUTE ||
                    effect == LED_ERROR || effect == LED_SOCKET_ERROR || effect == LED_VOLUME ||
                    effect == LED_STARTUP;
    if (!animated && effect == lastStaticEffect) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    switch (effect) {
      case LED_SPEAK: showRainbow(step, 4); break;
      case LED_THINK: showSolid(parseHexColor(cfg.ledThinkColor, CRGB::Blue)); break;
      case LED_LOADING: showRainbow(step, 10); break;
      case LED_MUTE: showMuteBreath(step); break;
      case LED_ERROR: showSolid((step % 50) < 25 ? CRGB::Red : CRGB::Black); break;
      case LED_SOCKET_ERROR: showSocketErrorBreath(step); break;
      case LED_STARTUP: showStartupEffect(step); break;
      case LED_PAUSE: showSolid(CRGB(255, 80, 0)); break;
      case LED_VOLUME: showVolumeLevel(cfg.volume); break;
      case LED_OFF: showSolid(CRGB::Black); break;
    }
    if (!animated) lastStaticEffect = effect;
    else lastStaticEffect = (LedEffect)255;
    step++;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

static void initButtons() {
  if (!cfg.buttonsEnabled) return;
  if (inputModeRotary()) {
    pinMode(cfg.pinRotaryClk, INPUT_PULLUP);
    pinMode(cfg.pinRotaryDt, INPUT_PULLUP);
    pinMode(cfg.pinRotarySw, INPUT_PULLUP);
    return;
  }
  pinMode(cfg.pinBtnMic, INPUT_PULLUP);
  pinMode(cfg.pinBtnVolUp, INPUT_PULLUP);
  pinMode(cfg.pinBtnVolDown, INPUT_PULLUP);
  pinMode(cfg.pinBtnWake, INPUT_PULLUP);
}

static void uninstallPcmTx() {
  if (!pcmTxInstalled) return;
  i2s_driver_uninstall(PCM_I2S_PORT);
  pcmTxInstalled = false;
}

static void stopMp3(bool keepSilentDac = true);
static void suspendMicForAudio();
static void resumeMicAfterAudio();

static void installSilentDac() {
  if (pcmTxInstalled) {
    i2s_zero_dma_buffer(PCM_I2S_PORT);
    return;
  }
  i2s_config_t i2sConfig = {};
  i2sConfig.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  i2sConfig.sample_rate = 16000;
  i2sConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2sConfig.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  i2sConfig.communication_format = VBOT_I2S_COMM_FORMAT;
  i2sConfig.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2sConfig.dma_buf_count = I2S_DMA_BUFFER_COUNT;
  i2sConfig.dma_buf_len = I2S_DMA_BUFFER_LENGTH;
  i2sConfig.use_apll = false;
  i2sConfig.tx_desc_auto_clear = true;
  i2s_pin_config_t pinConfig = {};
  pinConfig.bck_io_num = cfg.dacBclk;
  pinConfig.ws_io_num = cfg.dacLrc;
  pinConfig.data_out_num = cfg.dacDout;
  pinConfig.data_in_num = I2S_PIN_NO_CHANGE;
  esp_err_t err = i2s_driver_install(PCM_I2S_PORT, &i2sConfig, 0, nullptr);
  if (err != ESP_OK) {
    LOGF("Không cài được driver DAC I2S im lặng: %s (%d)", esp_err_to_name(err), err);
    return;
  }
  err = i2s_set_pin(PCM_I2S_PORT, &pinConfig);
  if (err != ESP_OK) {
    LOGF("Không thiết lập được GPIO DAC I2S: %s (%d)", esp_err_to_name(err), err);
    i2s_driver_uninstall(PCM_I2S_PORT);
    return;
  }
  err = i2s_zero_dma_buffer(PCM_I2S_PORT);
  if (err != ESP_OK) {
    LOGF("Không xóa được DMA DAC I2S: %s (%d)", esp_err_to_name(err), err);
    i2s_driver_uninstall(PCM_I2S_PORT);
    return;
  }
  pcmTxInstalled = true;
}

static void stopMp3(bool keepSilentDac) {
  if (mp3) {
    if (mp3->isRunning()) mp3->stop();
    delete mp3;
    mp3 = nullptr;
  }
  delete mp3File;
  mp3File = nullptr;
  delete mp3RawFile;
  mp3RawFile = nullptr;
  delete mp3Out;
  mp3Out = nullptr;
  if (startupAudioPlaying) {
    startupAudioPlaying = false;
    if (!recording) setLedEffect(micActive() ? LED_OFF : LED_MUTE);
  }
  if (keepSilentDac) installSilentDac();
}

static bool installPcmTx(uint32_t sampleRate) {
  uninstallPcmTx();
  stopMp3(false);
  i2s_config_t i2sConfig = {};
  i2sConfig.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  i2sConfig.sample_rate = sampleRate;
  i2sConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2sConfig.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  i2sConfig.communication_format = VBOT_I2S_COMM_FORMAT;
  i2sConfig.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2sConfig.dma_buf_count = I2S_DMA_BUFFER_COUNT;
  i2sConfig.dma_buf_len = I2S_DMA_BUFFER_LENGTH;
  i2sConfig.use_apll = false;
  i2sConfig.tx_desc_auto_clear = true;
  i2s_pin_config_t pinConfig = {};
  pinConfig.bck_io_num = cfg.dacBclk;
  pinConfig.ws_io_num = cfg.dacLrc;
  pinConfig.data_out_num = cfg.dacDout;
  pinConfig.data_in_num = I2S_PIN_NO_CHANGE;
  esp_err_t err = i2s_driver_install(PCM_I2S_PORT, &i2sConfig, 0, nullptr);
  if (err != ESP_OK) {
    LOGF("Quá trình cài đặt trình điều khiển PCM I2S thất bại: %d", err);
    return false;
  }
  err = i2s_set_pin(PCM_I2S_PORT, &pinConfig);
  if (err != ESP_OK) {
    LOGF("Quá trình thiết lập chân PCM I2S thất bại: %d", err);
    i2s_driver_uninstall(PCM_I2S_PORT);
    return false;
  }
  i2s_zero_dma_buffer(PCM_I2S_PORT);
  pcmTxInstalled = true;
  return true;
}

static void initAudioMp3() {
  uninstallPcmTx();
  stopMp3(false);
}

static bool playMp3FilePath(const char *path, bool autoContinueAfter = false, LedEffect playLedEffect = LED_SPEAK, bool speakLed = false);
static String encodeAudioUrl(const char *url);

static bool startMp3Source(const char *source, bool localFile, bool feedbackOnFail = true) {
  // Dọn phiên trước khi tạo source mới. Không gọi stopMp3() sau khi source mới
  // đã mở vì hàm đó sở hữu và xóa cả mp3File/mp3RawFile.
  stopMp3(false);
  audioOpening = true;
  String audioUrl(source);
  // Nhường DMA/RAM của mic khi giải mã và tránh thu tiếng loa vọng lại.
  suspendMicForAudio();
  // Giữ clock I2S và phát im lặng trong lúc mở source để MAX98357 thoát
  // standby trước khi decoder gửi mẫu MP3 đầu tiên.
  installSilentDac();
  uint32_t dacWarmupStarted = millis();
  //LOGF("Heap trước khi mở MP3: free=%u, min=%u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
  if (localFile) {
    mp3File = new (std::nothrow) AudioFileSourceLittleFS(source);
  } else if (audioUrl.startsWith("https://")) {
    mp3RawFile = new (std::nothrow) AudioFileSourceHTTPSStream(source);
  } else {
    mp3RawFile = new (std::nothrow) AudioFileSourceHTTPStream(source);
  }
  if (!localFile && mp3RawFile && mp3RawFile->isOpen()) {
    mp3File = new (std::nothrow) AudioFileSourceBuffer(mp3RawFile, MP3_STREAM_BUFFER_SIZE);
  }
  if (!mp3File || !mp3File->isOpen()) {
    LOGF("Không mở được MP3 %s: %s", localFile ? "từ bộ nhớ LittleFS" : "từ URL", source);
    stopMp3();
    //setLedEffect(LED_ERROR);
    if (feedbackOnFail) playMp3FilePath("/sound/dong.mp3");
    audioOpening = false;
    resumeMicAfterAudio();
    return false;
  }
  uint32_t warmupElapsed = millis() - dacWarmupStarted;
  if (warmupElapsed < DAC_MP3_WARMUP_MS) {
    vTaskDelay(pdMS_TO_TICKS(DAC_MP3_WARMUP_MS - warmupElapsed));
  }
  // AudioOutputI2S sẽ cài và sở hữu driver I2S trong thời gian giải mã.
  // Chỉ gỡ driver DAC im lặng. Source MP3 vừa mở phải còn nguyên cho begin().
  uninstallPcmTx();
  //LOGF("Đã mở source MP3: size=%u, free=%u, min=%u", (unsigned)mp3File->getSize(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
  mp3Out = new (std::nothrow) AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S);
  if (!mp3Out) {
    LOGF("Không đủ RAM để khởi tạo AudioOutputI2S: %s", source);
    stopMp3();
    setLedEffect(LED_ERROR);
    audioOpening = false;
    resumeMicAfterAudio();
    return false;
  }
  mp3Out->SetPinout(cfg.dacBclk, cfg.dacLrc, cfg.dacDout);
  mp3Out->SetGain((float)cfg.volume / 100.0f);
  mp3 = new (std::nothrow) AudioGeneratorMP3();
  if (!mp3) {
    LOGF("Không đủ RAM để khởi tạo AudioGeneratorMP3: %s", source);
    stopMp3();
    setLedEffect(LED_ERROR);
    audioOpening = false;
    resumeMicAfterAudio();
    return false;
  }
  if (!mp3->begin(mp3File, mp3Out)) {
    LOGF("Không bắt đầu giải mã MP3 được: %s, free=%u, largest=%u, min=%u",
         source,
         (unsigned)ESP.getFreeHeap(),
         (unsigned)ESP.getMaxAllocHeap(),
         (unsigned)ESP.getMinFreeHeap());
    stopMp3();
    setLedEffect(LED_ERROR);
    audioOpening = false;
    resumeMicAfterAudio();
    return false;
  }
  // mp3->begin() vừa cài lại I2S. Gửi silence qua chính AudioOutputI2S mới
  // trước khi gọi mp3->loop(), nếu không frame MP3 đầu tiên có thể bị phát
  // trong lúc ampli đang thoát shutdown.
  int16_t silentSample[2] = {0, 0};
  uint32_t outputWarmupDeadline = millis() + DAC_MP3_WARMUP_MS;
  while (!deadlineReached(outputWarmupDeadline)) {
    if (!mp3Out->ConsumeSample(silentSample)) vTaskDelay(pdMS_TO_TICKS(1));
  }
  //LOGF("Đã bắt đầu phát MP3: free=%u, min=%u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
  audioOpening = false;
  return true;
}

static bool startMp3Url(const char *url) {
  return startMp3Source(url, false);
}

static bool startMp3UrlWithFallback(const char *url, const char *originalUrl) {
  if (startMp3Source(url, false, false)) return true;

  // Lỗi mở HTTP có thể chỉ là TCP/proxy chậm nhất thời. stopMp3() bên trong
  // startMp3Source() đã đóng HTTPClient; nhường một nhịp rồi thử lại chính URL
  // trước khi đổi proxy để tránh bỏ phát dù endpoint vẫn đang hoạt động.
  LOGF("Proxy audio chưa mở được, thử lại cùng địa chỉ sau 300 ms");
  vTaskDelay(pdMS_TO_TICKS(300));
  if (WiFi.status() == WL_CONNECTED && startMp3Source(url, false, false)) return true;

  String fallbackUrl = fallbackAudioProxyUrl(originalUrl, url);
  if (!fallbackUrl.length()) {
    playMp3FilePath("/sound/dong.mp3");
    return false;
  }

  String encodedFallbackUrl = encodeAudioUrl(fallbackUrl.c_str());
  if (cfg.fallbackStreamApiToServer) {
    LOGF("URL stream proxy lỗi, thử lại bằng API server mặc định đang kết nối: %s", fallbackUrl.c_str());
  }
  return startMp3Source(encodedFallbackUrl.c_str(), false);
}

static bool startMp3File(const char *path) {
  return startMp3Source(path, true);
}

static void initMic() {
  if (!vbotServerStartupEnabled()) return;
  lockMicI2s();
  if (micRxInstalled) {
    i2s_driver_uninstall(MIC_I2S_PORT);
    micRxInstalled = false;
  }
  i2s_config_t i2sConfig = {};
  i2sConfig.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  i2sConfig.sample_rate = MIC_SAMPLE_RATE;
  i2sConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  i2sConfig.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2sConfig.communication_format = VBOT_I2S_COMM_FORMAT;
  i2sConfig.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2sConfig.dma_buf_count = 8;
  i2sConfig.dma_buf_len = FRAME_SAMPLES;
  i2sConfig.use_apll = false;
  i2sConfig.tx_desc_auto_clear = false;
  i2s_pin_config_t pinConfig = {};
  pinConfig.bck_io_num = cfg.micBclk;
  pinConfig.ws_io_num = cfg.micWs;
  pinConfig.data_out_num = I2S_PIN_NO_CHANGE;
  pinConfig.data_in_num = cfg.micDin;
  esp_err_t err = i2s_driver_install(MIC_I2S_PORT, &i2sConfig, 0, nullptr);
  if (err != ESP_OK) {
    LOGF("Quá trình cài đặt trình điều khiển MIC I2S thất bại: %d", err);
    cfg.micEnabled = false;
    unlockMicI2s();
    return;
  }
  err = i2s_set_pin(MIC_I2S_PORT, &pinConfig);
  if (err != ESP_OK) {
    LOGF("Lỗi thiết lập chân MIC I2S: %d", err);
    i2s_driver_uninstall(MIC_I2S_PORT);
    micRxInstalled = false;
    cfg.micEnabled = false;
    unlockMicI2s();
    return;
  }
  i2s_zero_dma_buffer(MIC_I2S_PORT);
  micRxInstalled = true;
  unlockMicI2s();
}

static void suspendMicForAudio() {
  if (!micRxInstalled) return;
  // Chặn task mic vào lượt đọc mới trước khi chờ lượt đọc hiện tại kết thúc.
  micSuspendedForAudio = true;
  vTaskDelay(pdMS_TO_TICKS(50));
  lockMicI2s();
  if (micRxInstalled) {
    i2s_driver_uninstall(MIC_I2S_PORT);
    micRxInstalled = false;
  }
  unlockMicI2s();
  //LOGF("Đã giải phóng MIC I2S: free=%u, min=%u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
}

static void resumeMicAfterAudio() {
  if (!vbotServerStartupEnabled() || !micSuspendedForAudio || audioOpening || micRxInstalled) return;
  //LOGF("Khôi phục MIC I2S sau khi phát MP3: free=%u, min=%u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
  micSuspendedForAudio = false;
  initMic();
}

static bool readMicFrame(MicFrame &frame) {
  size_t bytesRead = 0;
  if (audioOpening || micSuspendedForAudio || !micRxInstalled) return false;
  lockMicI2s();
  if (audioOpening || micSuspendedForAudio || !micRxInstalled) {
    unlockMicI2s();
    return false;
  }
  esp_err_t readResult = i2s_read(MIC_I2S_PORT, micRawBuffer, sizeof(micRawBuffer), &bytesRead, pdMS_TO_TICKS(40));
  unlockMicI2s();
  if (readResult != ESP_OK || bytesRead < sizeof(micRawBuffer)) {
    return false;
  }
  int16_t *out = reinterpret_cast<int16_t *>(frame.data);
  float gain = cfg.micGain;
  int32_t slotAPeak = 0;
  int32_t slotBPeak = 0;
  int64_t slotAAbsSum = 0;
  int64_t slotBAbsSum = 0;
  int32_t peak = 0;
  int64_t absSum = 0;
  for (size_t i = 0; i < FRAME_SAMPLES; i++) {
    int32_t slotA = micRawBuffer[i * 2];
    int32_t slotB = micRawBuffer[i * 2 + 1];
    int32_t slotAAbs = slotA < 0 ? -slotA : slotA;
    int32_t slotBAbs = slotB < 0 ? -slotB : slotB;
    if (slotAAbs > slotAPeak) slotAPeak = slotAAbs;
    if (slotBAbs > slotBPeak) slotBPeak = slotBAbs;
    slotAAbsSum += slotAAbs;
    slotBAbsSum += slotBAbs;
    bool preferSlotB = cfg.micChannel[0] != 'R' && cfg.micChannel[0] != 'r';
    int32_t raw = preferSlotB ? slotB : slotA;
    int32_t selectedAbs = preferSlotB ? slotBAbs : slotAAbs;
    int32_t otherRaw = preferSlotB ? slotA : slotB;
    int32_t otherAbs = preferSlotB ? slotAAbs : slotBAbs;
    if (selectedAbs == 0 && otherAbs > 0) raw = otherRaw;
    int32_t s = (int32_t)((raw >> 16) * gain);
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    int32_t absSample = s < 0 ? -s : s;
    if (absSample > peak) peak = absSample;
    absSum += absSample;
    out[i] = (int16_t)s;
  }
  static uint32_t lastMicStatsLog = 0;

  /*
  if (cfg.serialLogs && millis() - lastMicStatsLog >= MIC_DEBUG_INTERVAL_MS) {
    LOGF("[DEBUG] Thông số micrô raw0=%ld slotA_peak=%ld slotA_avg=%ld slotB_peak=%ld slotB_avg=%ld pcm_peak=%ld pcm_avg=%ld gain=%.2f ch=%s",
         (long)micRawBuffer[0],
         (long)slotAPeak,
         (long)(slotAAbsSum / FRAME_SAMPLES),
         (long)slotBPeak,
         (long)(slotBAbsSum / FRAME_SAMPLES),
         (long)peak,
         (long)(absSum / FRAME_SAMPLES),
         cfg.micGain,
         cfg.micChannel);
    lastMicStatsLog = millis();
  }
  */
  return true;
}

static void micTask(void *) {
  for (;;) {
    // Một phiên STT đang chạy phải được ưu tiên hơn lưu lượng MQTT; tạm dừng
    // microphone giữa phiên sẽ khiến Google Cloud hủy stream vì thiếu audio.
    if (!otaUpdateActive && wsReady && (!mqttNetworkBusy || recording) &&
        micActive() && readMicFrame(micWorkFrame)) {
      if (xQueueSend(micQueue, &micWorkFrame, 0) != pdTRUE) {
        xQueueReceive(micQueue, &micDropFrame, 0);
        xQueueSend(micQueue, &micWorkFrame, 0);
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(20));
    }
  }
}

static void releaseAudioCommandResources(const AudioCommand &cmd) {
  if (cmd.pcmPoolSlot >= 0 && pcmFreeQueue) {
    uint8_t slot = (uint8_t)cmd.pcmPoolSlot;
    xQueueSend(pcmFreeQueue, &slot, 0);
  }
}

static void clearPendingAudioCommands() {
  if (!audioQueue) return;
  AudioCommand pending;
  while (xQueueReceive(audioQueue, &pending, 0) == pdTRUE) {
    releaseAudioCommandResources(pending);
  }
}

static bool queueAudioCommand(const AudioCommand &cmd) {
  if (!hardwareGpioValid || fsMaintenanceActive) {
    releaseAudioCommandResources(cmd);
    return false;
  }
  if (!audioQueue) {
    releaseAudioCommandResources(cmd);
    return false;
  }
  AudioCommand dropped;
  if (xQueueSend(audioQueue, &cmd, 0) != pdTRUE) {
    if (xQueueReceive(audioQueue, &dropped, 0) == pdTRUE) releaseAudioCommandResources(dropped);
    if (xQueueSend(audioQueue, &cmd, 0) != pdTRUE) {
      releaseAudioCommandResources(cmd);
      return false;
    }
  }
  return true;
}

static bool validAudioUrl(const char *url) {
  if (!url || !*url) return false;
  String u(url);
  return u != "None" && u != "null" && (u.startsWith("http://") || u.startsWith("https://"));
}

static bool validMp3FilePath(const char *path) {
  if (!path || path[0] != '/') return false;
  String p(path);
  return p.endsWith(".mp3") && LittleFS.exists(path);
}

static void refreshLocalSoundCache() {
  localDingAvailable = validMp3FilePath("/sound/ding.mp3");
  localDongAvailable = validMp3FilePath("/sound/dong.mp3");
  localMicOnAvailable = validMp3FilePath("/sound/mic_on.mp3");
  localMicOffAvailable = validMp3FilePath("/sound/mic_off.mp3");
  localTutTutAvailable = validMp3FilePath("/sound/tut_tut.mp3");
  localMusicStopsAvailable = validMp3FilePath("/sound/music_stops.mp3");
  localStartupAvailable = validMp3FilePath("/sound/start_up.mp3");
}

static bool isDefaultSoundUrl(const char *url) {
  if (!url) return false;
  String u(url);
  return u.indexOf("/assets/sound/default/") >= 0;
}

static const char *localDefaultSoundPath(const char *url) {
  if (!url) return nullptr;
  if (localDingAvailable && strstr(url, "/assets/sound/default/ding.mp3")) return "/sound/ding.mp3";
  if (localDongAvailable && strstr(url, "/assets/sound/default/dong.mp3")) return "/sound/dong.mp3";
  return nullptr;
}

static bool isUrlSafeChar(char c) {
  return (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') ||
         c == '-' || c == '_' || c == '.' || c == '~' ||
         c == ':' || c == '/' || c == '?' || c == '&' ||
         c == '=' || c == '%' || c == '#';
}

static String encodeAudioUrl(const char *url) {
  String out;
  static const char hex[] = "0123456789ABCDEF";
  for (const char *p = url; p && *p; p++) {
    uint8_t c = (uint8_t)*p;
    if (isUrlSafeChar((char)c)) {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

static String conversationModeOnFeedbackUrl() {
  return String("http://") + cfg.serverHost + "/assets/sound/default/conversation_mode_on.mp3";
}

static String conversationModeOffFeedbackUrl() {
  return String("http://") + cfg.serverHost + "/assets/sound/default/conversation_mode_off.mp3";
}

static String dongFeedbackUrl() {
  return String("http://") + cfg.serverHost + "/assets/sound/default/dong.mp3";
}

static String wifiAllResetFeedbackUrl() {
  return String("http://") + cfg.serverHost + "/assets/sound/default/wifi_all_reset.mp3";
}

static String wifiResetOkFeedbackUrl() {
  return String("http://") + cfg.serverHost + "/assets/sound/default/wifi_reset_ok.mp3";
}

static bool audioIdle() {
  return !audioOpening && (!mp3 || !mp3->isRunning()) &&
         !pcmPlaybackActive && (millis() - lastPcmPlaybackMs >= 300);
}

//static void playMp3FilePath(const char *path, bool autoContinueAfter = false, LedEffect playLedEffect = LED_SPEAK, bool speakLed = false) {
static bool playMp3FilePath(const char *path, bool autoContinueAfter, LedEffect playLedEffect, bool speakLed) {
  if (!path || !*path) return false;
  if (playLedEffect == LED_STARTUP && speakLed) startupAudioPlaying = true;
  AudioCommand cmd = {};
  cmd.type = AUDIO_CMD_MP3_FILE;
  cmd.speakLed = speakLed;
  cmd.autoContinueAfter = autoContinueAfter;
  cmd.playLedEffect = playLedEffect;
  strlcpy(cmd.url, path, sizeof(cmd.url));
  bool queued = queueAudioCommand(cmd);
  if (!queued && playLedEffect == LED_STARTUP && speakLed) startupAudioPlaying = false;
  return queued;
}

static bool playMp3Url(const char *url, bool autoContinueAfter = false, LedEffect playLedEffect = LED_SPEAK) {
  if (!validAudioUrl(url)) return false;
  String playableUrl = effectiveAudioProxyUrl(url);
  AudioCommand cmd = {};
  cmd.autoContinueAfter = autoContinueAfter;
  cmd.playLedEffect = playLedEffect;
  const char *localPath = localDefaultSoundPath(playableUrl.c_str());
  if (localPath) {
    cmd.type = AUDIO_CMD_MP3_FILE;
    cmd.speakLed = false;
    strlcpy(cmd.url, localPath, sizeof(cmd.url));
  } else {
    cmd.type = AUDIO_CMD_MP3_URL;
    cmd.speakLed = !isDefaultSoundUrl(playableUrl.c_str());
    // Chỉ giữ URL gốc trong queue. URL proxy lồng nhau có thể nở rất dài sau
    // percent-encoding và từng bị cắt ở 319 byte trước khi DAC xử lý.
    if (strlen(url) >= sizeof(cmd.url)) {
      LOGF("URL âm thanh quá dài (%u/%u byte)", (unsigned)strlen(url), (unsigned)(sizeof(cmd.url) - 1));
      return false;
    }
    strlcpy(cmd.url, url, sizeof(cmd.url));
  }
  return queueAudioCommand(cmd);
}

static String googleTranslateTtsUrl(const char *text) {
  return String("https://translate.google.com/translate_tts")
    + "?ie=UTF-8&tl=vi-VN&client=tw-ob&ttsspeed=1.0&q="
    + encodeQueryParam(String(text ? text : ""));
}

static bool queueGoogleTranslateTts(const String &input) {
  String text = input;
  text.trim();
  if (!text.length() || text.length() > GOOGLE_TTS_MAX_TEXT_BYTES) return false;
  AudioCommand cmd = {};
  cmd.type = AUDIO_CMD_GOOGLE_TTS;
  cmd.playLedEffect = LED_SPEAK;
  cmd.speakLed = true;
  strlcpy(cmd.url, text.c_str(), sizeof(cmd.url));
  lockMediaState();
  currentGoogleTtsText = text;
  googleTtsState = 1;
  googleTtsUpdatedAtMs = millis();
  unlockMediaState();
  mqttStateDirty = true;
  // TTS là thông báo ưu tiên: bỏ các lệnh âm thanh chưa xử lý. Khi DAC nhận
  // lệnh này, startMp3Source() tự dừng nguồn đang phát trước khi mở proxy.
  clearPendingAudioCommands();
  bool queued = queueAudioCommand(cmd);
  if (!queued) {
    lockMediaState();
    googleTtsState = 3;
    googleTtsUpdatedAtMs = millis();
    unlockMediaState();
  }
  return queued;
}

static bool stopAudioPlayback(bool clearSuspendedAudio = true) {
  AudioCommand cmd = {};
  cmd.type = AUDIO_CMD_STOP;
  cmd.clearSuspendedAudio = clearSuspendedAudio;
  return queueAudioCommand(cmd);
}

static bool stopAudioPlaybackWithFeedback(bool clearSuspendedAudio = true) {
  if (!stopAudioPlayback(clearSuspendedAudio)) return false;
  return !localMusicStopsAvailable || playMp3FilePath("/sound/music_stops.mp3");
}

static bool beginWifiResetSequence(bool resetConfigToo) {
  if (otaUpdateActive || fsMaintenanceActive) {
    UART_LOGF("Từ chối %s vì OTA/LittleFS đang được cập nhật.", resetConfigToo ? "factory reset" : "reset WiFi");
    return false;
  }
  UART_LOGF("Bắt đầu chuỗi %s từ WebUI", resetConfigToo ? "factory reset" : "reset WiFi");
  stopAudioPlayback(true);
  playMp3Url(wifiAllResetFeedbackUrl().c_str());
  uint32_t when = millis() + 2500;
  if (resetConfigToo) pendingFactoryResetAtMs = when;
  else pendingWifiResetAtMs = when;
  return true;
}

static void runPendingResetSequences() {
  uint32_t now = millis();
  if (volumeSavePending && deadlineReached(volumeSaveAtMs, now)) {
    uint8_t value = volumeSaveValue;
    bool playFeedback = volumeSaveFeedbackPending;
    volumeSavePending = false;
    volumeSaveFeedbackPending = false;
    lockNvs();
    prefs.putUChar("vol", value);
    unlockNvs();
    LOGF("Đã lưu âm lượng vào NVS: %u", value);
    if (playFeedback && value > 0 && waitingToWake && !recording && audioIdle() && localTutTutAvailable) {
      playMp3FilePath("/sound/tut_tut.mp3");
    }
  }
  if (ledBrightnessSavePending && deadlineReached(ledBrightnessSaveAtMs, now)) {
    uint8_t value = ledBrightnessSaveValue;
    ledBrightnessSavePending = false;
    lockNvs();
    prefs.putUChar("ledbr", value);
    unlockNvs();
    LOGF("Đã lưu độ sáng đèn LED vào NVS: %u", value);
  }
  if (pendingFactoryResetAtMs && deadlineReached(pendingFactoryResetAtMs, now)) {
    pendingFactoryResetAtMs = 0;
    if (resetAllNvs()) {
      playMp3Url(wifiResetOkFeedbackUrl().c_str());
      scheduleRestart(2500, "Hoàn tất factory reset từ WebUI");
    } else {
      UART_LOG("Factory reset từ WebUI thất bại; đã hủy restart để tránh báo thành công sai.");
    }
  }
  if (pendingWifiResetAtMs && deadlineReached(pendingWifiResetAtMs, now)) {
    pendingWifiResetAtMs = 0;
    WiFiManager wm;
    wm.resetSettings();
    bool wifiOk = !wm.getWiFiIsSaved();
    UART_LOGF("Reset WiFi từ WebUI: %s", wifiOk ? "thành công" : "THẤT BẠI");
    if (wifiOk) {
      playMp3Url(wifiResetOkFeedbackUrl().c_str());
      scheduleRestart(2500, "Hoàn tất reset WiFi từ WebUI");
    } else {
      UART_LOG("Reset WiFi từ WebUI thất bại; đã hủy restart.");
    }
  }
}

static TaskHandle_t createPinnedTaskRequired(
  TaskFunction_t task,
  const char *name,
  uint32_t stackDepth,
  UBaseType_t priority,
  BaseType_t coreId
) {
  TaskHandle_t handle = nullptr;
  BaseType_t ok = xTaskCreatePinnedToCore(task, name, stackDepth, nullptr, priority, &handle, coreId);
  if (ok != pdPASS || !handle) {
    char message[96];
    snprintf(message, sizeof(message), "Khong tao duoc task %s, stack=%lu, core=%ld", name, (unsigned long)stackDepth, (long)coreId);
    fatalHalt(message);
  }
  return handle;
}

static void queuePcmBinary(uint8_t *payload, size_t length) {
  if (!payload || !length) return;
  const char *audioFormat = pendingPcmMeta["audio_format"] | "pcm_s16le";
  if (strcmp(audioFormat, "pcm_s16le") != 0) return;
  uint8_t channels = pendingPcmMeta["channels"] | 1;
  if (channels != 1) return;
  uint8_t sampleWidth = pendingPcmMeta["sample_width"] | 2;
  if (sampleWidth != 2) return;
  size_t expectedBytes = pendingPcmMeta["audio_bytes"] | 0;
  if (expectedBytes && expectedBytes != length) return;
  if (length & 1) length--;
  if (!length) return;
  if (length > PCM_POOL_BUFFER_SIZE || !pcmFreeQueue) {
    LOGF("Bỏ qua gói PCM %u byte (giới hạn pool %u byte)", (unsigned)length, (unsigned)PCM_POOL_BUFFER_SIZE);
    setLedEffect(LED_ERROR);
    return;
  }
  uint8_t slot = 0;
  if (xQueueReceive(pcmFreeQueue, &slot, 0) != pdTRUE) return;
  memcpy(pcmPool[slot], payload, length);
  AudioCommand cmd = {};
  cmd.type = AUDIO_CMD_PCM_CHUNK;
  cmd.pcm = pcmPool[slot];
  cmd.pcmPoolSlot = (int8_t)slot;
  cmd.pcmLen = length;
  cmd.sampleRate = pendingPcmMeta["sample_rate"] | 16000;
  queueAudioCommand(cmd);
}

static bool pcmPlaybackRecentlyActive() {
  // Giữ LED_SPEAK xuyên qua khoảng trống giữa các chunk. Ngưỡng xác định audio
  // idle vẫn là 300 ms để chế độ hội thoại không bị chậm thêm.
  return pcmPlaybackActive || (millis() - lastPcmPlaybackMs < PCM_LED_HOLD_MS);
}

static inline int16_t clamp16(int32_t v) {
  if (v > 32767) return 32767;
  if (v < -32768) return -32768;
  return (int16_t)v;
}

static void amplifyPcm16(uint8_t *data, size_t len, uint8_t volume) {
  if (!data || len < sizeof(int16_t)) return;
  float floatGain = ((float)volume / 100.0f) * cfg.pcmVolumeBoost;
  int32_t gainFixed = (int32_t)(floatGain * 256.0f + 0.5f);
  int16_t *samples = (int16_t *)data;
  size_t count = len / sizeof(int16_t);
  for (size_t i = 0; i < count; i++) {
    int64_t v = (int64_t)samples[i] * gainFixed;
    samples[i] = clamp16((int32_t)((v + 128) >> 8));
  }
}

// Xử lý các lệnh âm thanh từ hàng đợi, bao gồm cả việc phát MP3 từ URL hoặc file cục bộ, phát PCM thô realtime, điều chỉnh volume và dừng âm thanh đang phát. Task này cũng đảm bảo tự động tiếp tục cuộc hội thoại nếu được yêu cầu sau khi một đoạn âm thanh kết thúc và cập nhật hiệu ứng LED tương ứng với trạng thái âm thanh hiện tại
static void dacTask(void *) {
  AudioCommand cmd;
  for (;;) {
    while (xQueueReceive(audioQueue, &cmd, 0) == pdTRUE) {
      if (fsMaintenanceActive) {
        releaseAudioCommandResources(cmd);
        continue;
      }
      //phát MP3 từ URL, Local file
      if (cmd.type == AUDIO_CMD_MP3_URL || cmd.type == AUDIO_CMD_MP3_FILE) {
        //LOGF("Audio URL: %s", cmd.url);
        pendingAutoContinue = false;
        lockMediaState();
        if (googleTtsState == 2) {
          googleTtsState = 0;
          googleTtsUpdatedAtMs = millis();
        }
        unlockMediaState();
        bool started = false;
        if (cmd.type == AUDIO_CMD_MP3_FILE) {
          started = startMp3File(cmd.url);
        } else {
          String proxyUrl = effectiveAudioProxyUrl(cmd.url);
          String encodedUrl = encodeAudioUrl(proxyUrl.c_str());
          started = startMp3UrlWithFallback(encodedUrl.c_str(), cmd.url);
        }
        if (started) {
          lockMediaState();
          currentAudioUrl = cmd.url;
          unlockMediaState();
          mqttStateDirty = true;
          if (cmd.speakLed && !volumeLedActive && !recording) setLedEffect(cmd.playLedEffect);
          if (cmd.autoContinueAfter) pendingAutoContinue = true;
        } else {
          // Không để hội thoại mắc kẹt nếu URL/decoder MP3 lỗi. dacTask sẽ gửi
          // Skip_WakeUP khi audio đã idle, giống như sau khi phát thành công.
          pendingAutoContinue = cmd.autoContinueAfter;
        }
      }
      else if (cmd.type == AUDIO_CMD_GOOGLE_TTS) {
        pendingAutoContinue = false;
        String originalUrl = googleTranslateTtsUrl(cmd.url);
        String proxyUrl = effectiveAudioProxyUrl(originalUrl.c_str());
        String encodedUrl = encodeAudioUrl(proxyUrl.c_str());
        bool started = startMp3UrlWithFallback(encodedUrl.c_str(), originalUrl.c_str());
        lockMediaState();
        if (started) {
          currentAudioUrl = originalUrl;
          currentMqttMediaTitle = "Google Translate TTS";
          googleTtsState = 2;
          googleTtsUpdatedAtMs = millis();
        } else {
          googleTtsState = 3;
          googleTtsUpdatedAtMs = millis();
        }
        unlockMediaState();
        if (started && !volumeLedActive && !recording) setLedEffect(LED_SPEAK);
        mqttStateDirty = true;
      }
      //Phát PCM raw thô realtime từ máy chủ VBot
      else if (cmd.type == AUDIO_CMD_PCM_CHUNK) {
        pcmPlaybackActive = true;
        if (!pcmTxInstalled && !installPcmTx(cmd.sampleRate)) {
          LOG("Khởi tạo DAC PCM thất bại");
          pcmPlaybackActive = false;
          setLedEffect(LED_ERROR);
        } else {
          size_t written = 0;
          //Áp dụng volume chung cho PCM realtime
          amplifyPcm16((uint8_t *)cmd.pcm, cmd.pcmLen, cfg.volume);
          esp_err_t writeResult = i2s_write(
            PCM_I2S_PORT, cmd.pcm, cmd.pcmLen, &written, pdMS_TO_TICKS(500));
          if (writeResult != ESP_OK || written != cmd.pcmLen) {
            LOGF("DAC I2S write failed: %s (%d), wrote=%u/%u",
                 esp_err_to_name(writeResult), writeResult,
                 (unsigned)written, (unsigned)cmd.pcmLen);
            pcmPlaybackActive = false;
            uninstallPcmTx();
            setLedEffect(LED_ERROR);
          } else {
            lastPcmPlaybackMs = millis();
            if (!recording) setLedEffect(LED_SPEAK);
          }
        }
        if (cmd.pcmPoolSlot >= 0 && pcmFreeQueue) {
          uint8_t slot = (uint8_t)cmd.pcmPoolSlot;
          xQueueSend(pcmFreeQueue, &slot, 0);
        }
      }

      else if (cmd.type == AUDIO_CMD_SET_VOLUME) {
        if (mp3Out) mp3Out->SetGain((float)cfg.volume / 100.0f);
      }
      //Dừng âm thanh đang phát, bao gồm MP3 và PCM
      else if (cmd.type == AUDIO_CMD_STOP) {
        pcmPlaybackActive = false;
        pendingAutoContinue = false;
        stopMp3();
        lockMediaState();
        currentAudioUrl = "";
        if (googleTtsState != 0) {
          googleTtsState = 0;
          googleTtsUpdatedAtMs = millis();
        }
        unlockMediaState();
        mqttStateDirty = true;
        resumeMicAfterAudio();
      }
    }
    if (mp3 && mp3->isRunning()) {
      if (!mp3->loop()) {
        stopMp3();
        lockMediaState();
        currentAudioUrl = "";
        if (googleTtsState == 2) {
          googleTtsState = 0;
          googleTtsUpdatedAtMs = millis();
        }
        unlockMediaState();
        mqttStateDirty = true;
        resumeMicAfterAudio();
      }
    }
    //auto stop PCM playback nếu đã lâu không nhận được chunk mới
    if (pcmPlaybackActive && uxQueueMessagesWaiting(audioQueue) == 0 && millis() - lastPcmPlaybackMs >= 300) {
      pcmPlaybackActive = false;
      mqttStateDirty = true;
    }
    if (pendingAutoContinue && wsReady && audioIdle()) {
      pendingAutoContinue = false;
      wakeSkipRequest = true;
    }
    if (!recording && (!mp3 || !mp3->isRunning()) && !pcmPlaybackRecentlyActive()
        && (ledEffect == LED_SPEAK || ledEffect == LED_STARTUP) && deadlineReached(testLedUntilMs)) {
      setLedEffect(micActive() ? LED_OFF : LED_MUTE);
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

//Gửi dữ liệu cấu hình ban đầu của client tới máy chủ VBot sau khi kết nối WebSocket được thiết lập, bao gồm cả session ID duy nhất cho phiên làm việc này và các tuỳ chọn cấu hình khác để máy chủ biết cách xử lý âm thanh và phản hồi phù hợp
static void sendClientConfig(bool createNewSession = true) {
  lockCfg();
  String sessionPrefix = sessionPrefixFromClientName();
  strlcpy(cfg.sessionId, sessionPrefix.c_str(), sizeof(cfg.sessionId));
  unlockCfg();
  if (createNewSession || activeSessionId.isEmpty()) {
    activeSessionId = sessionPrefix + "_" + String((uint32_t)ESP.getEfuseMac(), HEX) + "_" + String((uint32_t)esp_random(), HEX);
  }
  JsonDocument doc;
  doc["session_id"] = activeSessionId;
  doc["client_name"] = cfg.clientName;
  doc["working_mode"] = cfg.workingMode;
  doc["conversation_mode"] = cfg.conversationMode;
  String out;
  serializeJson(doc, out);
  LOGF("Gửi cấu hình tới máy chủ VBot: %s", out.c_str());
  ws.sendTXT(out);
}

//Nhận dữ liệu trả về tự máy chủ VBot, bao gồm cả các sự kiện trạng thái và chunk PCM thô
static void handleJsonMessage(uint8_t *payload, size_t length) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    if (cfg.serialLogs) {
      Serial.print("[Máy Chủ VBot]: ");
      Serial.write(payload, length);
      Serial.println();
    }
    LOGF("Lỗi phân tích JSON: %s", err.c_str());
    return;
  }
  const char *process = doc["processing_process"] | "";
  if (!strcmp(process, "pcm_raw_audio")) {
    pendingPcmMeta.clear();
    pendingPcmMeta.set(doc);
    pendingPcmBinary = true;
    if (!pcmRawReceiveLogged) {
      pcmRawReceiveLogged = true;
      LOGF("Đang nhận PCM raw audio từ máy chủ VBot: rate=%dHz, channels=%d, sample_width=%d",
           (int)(doc["sample_rate"] | 16000),
           (int)(doc["channels"] | 1),
           (int)(doc["sample_width"] | 2));
    }
    return;
  }
  // Metadata PCM và binary luôn phải là một cặp liền nhau. Nếu một JSON trạng
  // thái khác đã tới thì metadata cũ không còn hợp lệ và không được ghép với
  // binary của một phản hồi sau.
  if (pendingPcmBinary) {
    pendingPcmBinary = false;
    pendingPcmMeta.clear();
    LOG("Bỏ metadata PCM không có binary tương ứng từ máy chủ.");
  }
  pcmRawReceiveLogged = false;
  if (cfg.serialLogs) {
    Serial.print("[Máy Chủ VBot]: ");
    Serial.write(payload, length);
    Serial.println();
  }
  if (!strcmp(process, "wake_word_detected")) {
    recording = true;
    recordingDeadlineMs = 0;
    waitingToWake = false;
    pendingAutoContinue = false;
    playMp3Url(doc["status_audio"] | "");
    setLedEffect(LED_THINK);
    return;
  }
  if (!strcmp(process, "recording")) {
    recording = true;
    uint32_t maximumRecordingMs = doc["maximum_recording_time_ms"] | 5000U;
    maximumRecordingMs = constrain(maximumRecordingMs, 1000U, 60000U);
    recordingDeadlineMs = millis() + maximumRecordingMs;
    waitingToWake = false;
    setLedEffect(LED_THINK);
    return;
  }
  if (!strcmp(process, "data_processing")) {
    recording = false;
    recordingDeadlineMs = 0;
    waitingToWake = false;
    setLedEffect(LED_LOADING);
    return;
  }
  if (!strcmp(process, "waiting_to_wake_up")) {
    recording = false;
    recordingDeadlineMs = 0;
    waitingToWake = true;
    pendingAutoContinue = false;
    restoreTemporaryMic();
    const char *tts = doc["tts_audio"] | "";
    const char *status = doc["status_audio"] | "";
    const char *audioUrl = validAudioUrl(tts) ? tts : status;
    if (!pcmPlaybackRecentlyActive() && uxQueueMessagesWaiting(audioQueue) == 0) {
      setLedEffect(micActive() ? LED_OFF : LED_MUTE);
    }
    if (validAudioUrl(audioUrl)) playMp3Url(audioUrl);
    return;
  }
  if (!strcmp(process, "continue_wake_up")) {
    recording = false;
    recordingDeadlineMs = 0;
    waitingToWake = false;
    const char *tts = doc["tts_audio"] | "";
    const char *status = doc["status_audio"] | "";
    bool hasAudio = validAudioUrl(tts) || validAudioUrl(status);
    pendingAutoContinue = false;
    if (!pcmPlaybackRecentlyActive() && uxQueueMessagesWaiting(audioQueue) == 0) {
      setLedEffect(micActive() ? LED_OFF : LED_MUTE);
    }
    if (hasAudio) {
      playMp3Url(validAudioUrl(tts) ? tts : status, cfg.conversationMode);
    } else if (cfg.conversationMode) {
      // Phản hồi main_processing có thể là PCM raw. Chờ DAC phát hết hàng đợi
      // rồi mới yêu cầu lượt thu tiếp để không cắt mất phần cuối câu trả lời.
      pendingAutoContinue = true;
    }
    return;
  }
  if (!strcmp(process, "chatbot_response")) {
    recording = false;
    recordingDeadlineMs = 0;
    waitingToWake = true;
    pendingAutoContinue = false;
    restoreTemporaryMic();
    const char *tts = doc["tts_audio"] | "";
    if (validAudioUrl(tts)) playMp3Url(tts);
    return;
  }
  if (!strcmp(process, "stt_to_tts_response")) {
    recording = false;
    recordingDeadlineMs = 0;
    waitingToWake = true;
    pendingAutoContinue = false;
    restoreTemporaryMic();
    setLedEffect(LED_OFF);
    return;
  }
  if (!strcmp(process, "error")) {
    recording = false;
    recordingDeadlineMs = 0;
    waitingToWake = true;
    pendingAutoContinue = false;
    restoreTemporaryMic();
    if (localDongAvailable) {
      playMp3FilePath("/sound/dong.mp3");
    } else {
      playMp3Url(dongFeedbackUrl().c_str());
    }
    setLedEffect(micActive() ? LED_OFF : LED_MUTE);
  }
}

static constexpr uint32_t WS_RECONNECT_INTERVAL_MS = 3000;
static uint32_t hotwordBackpressureUntilMs = 0;
static volatile bool wsStopRequest = false;

static void wsEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      ws.resetPendingMicFrame();
      wsStopRequest = false;
      wsReady = false;
      wsBackpressureSinceMs = 0;
      wsForceReconnectAtMs = 0;
      pendingPcmBinary = false;
      pendingPcmMeta.clear();
      if (micQueue) xQueueReset(micQueue);
      LOG("Đã kết nối WebSocket");
      setLedEffect(micActive() ? LED_OFF : LED_MUTE);
      sendClientConfig();
      wsReady = true;
      break;
    case WStype_DISCONNECTED:
      ws.resetPendingMicFrame();
      wsStopRequest = false;
      wsReady = false;
      wsBackpressureSinceMs = 0;
      pendingPcmBinary = false;
      pendingPcmMeta.clear();
      recording = false;
      recordingDeadlineMs = 0;
      waitingToWake = true;
      pendingAutoContinue = false;
      restoreTemporaryMic();
      if (!wsForceReconnectAtMs) wsForceReconnectAtMs = millis() + WS_RECONNECT_INTERVAL_MS;
      LOGF("WebSocket chưa kết nối; thử lại sau %u giây", (unsigned)(WS_RECONNECT_INTERVAL_MS / 1000));
      setLedEffect(LED_SOCKET_ERROR);
      break;
    case WStype_TEXT:
      handleJsonMessage(payload, length);
      break;
    case WStype_BIN:
      //LOGF("WS binary len=%u pending_pcm=%d", (unsigned)length, pendingPcmBinary ? 1 : 0);
      if (pendingPcmBinary) {
        queuePcmBinary(payload, length);
        pendingPcmBinary = false;
      }
      break;
    default:
      break;
  }
}

static void connectSocket() {
  if (!vbotServerStartupEnabled()) return;
  wsLastReconnectAttemptMs = millis();
  ws.disconnect();
  ws.begin(cfg.serverHost, cfg.serverPort, cfg.serverPath);
  ws.onEvent(wsEvent);
  ws.setReconnectInterval(WS_RECONNECT_INTERVAL_MS);
  ws.enableHeartbeat(15000, 3000, 2);
  LOGF("Địa chỉ kết nối máy chủ WebSocket ws://%s:%u%s", cfg.serverHost, cfg.serverPort, cfg.serverPath);
}

static void websocketTask(void *) {
  uint32_t lastMicLog = 0;
  for (;;) {
    // A WebSocket frame must remain contiguous on TCP. The mic frame writer
    // uses non-blocking raw send(), so finish any partial frame before
    // WebSocketsClient::loop() can emit ping/pong/control frames.
    if (wsReady && ws.hasPendingMicFrame()) {
      int pendingResult = ws.flushPendingMicFrame();
      if (pendingResult < 0) {
        ws.disconnect();
        wsBackpressureSinceMs = 0;
      } else if (ws.hasPendingMicFrame()) {
        if (!wsBackpressureSinceMs) wsBackpressureSinceMs = millis();
        if (millis() - wsBackpressureSinceMs >= 2000) {
          UART_LOGF("WebSocket có frame mic gửi dở quá 2 giây; đóng socket để tránh hỏng frame. recording=%d free=%u",
                    recording ? 1 : 0, (unsigned)ESP.getFreeHeap());
          wsForceReconnectAtMs = millis() + WS_RECONNECT_INTERVAL_MS;
          ws.disconnect();
          wsBackpressureSinceMs = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
        continue;
      } else {
        wsBackpressureSinceMs = 0;
      }
    }
    ws.loop();
    uint32_t wsNow = millis();
    if (!wsReady && !otaUpdateActive && WiFi.isConnected() &&
        wsForceReconnectAtMs && deadlineReached(wsForceReconnectAtMs, wsNow) &&
        wsNow - wsLastReconnectAttemptMs >= WS_RECONNECT_INTERVAL_MS) {
      // WebSocketsClient normally reconnects itself. Re-run begin() as a
      // watchdog because a socket closed during a partial non-blocking frame
      // can otherwise remain forever in the disconnected state.
      wsForceReconnectAtMs = wsNow + 15000;
      UART_LOGF("WebSocket watchdog: chủ động kết nối lại ws://%s:%u%s",
                cfg.serverHost, cfg.serverPort, cfg.serverPath);
      connectSocket();
    }
    if (clientConfigSyncRequested && wsReady) {
      clientConfigSyncRequested = false;
      sendClientConfig(false);
    }
    if (wsStopRequest && wsReady) {
      wsStopRequest = false;
      ws.sendTXT("stop");
    }
    if (wakeSkipRequest && wsReady) {
      wakeSkipRequest = false;
      if (!cfg.micEnabled) temporaryMicEnabled = true;
      stopAudioPlayback(true);
      ws.sendTXT("Skip_WakeUP");
      recording = true;
      recordingDeadlineMs = 0;
      waitingToWake = false;
      pendingAutoContinue = false;
      LOG("Đã gửi lệnh đánh thức, đang thu âm thanh...");
      setLedEffect(LED_THINK);
    }
    if (wsReady && recording && recordingDeadlineMs != 0 &&
        (int32_t)(millis() - recordingDeadlineMs) >= 0) {
      // Chủ động kết thúc đúng thời hạn server công bố. Việc ngừng gửi binary
      // ngay sau "stop" giữ ranh giới phiên PCM rõ ràng ở cả hai phía.
      ws.sendTXT("stop");
      recording = false;
      recordingDeadlineMs = 0;
      LOG("Đã gửi lệnh kết thúc thu âm tới máy chủ VBot.");
    }
    // Server chỉ nhận hotword PCM khi client thực sự ở trạng thái chờ đánh
    // thức. Gửi trong data_processing/TTS làm đầy TCP send buffer (EAGAIN 11).
    const bool hotwordBackpressurePaused = hotwordBackpressureUntilMs &&
      !deadlineReached(hotwordBackpressureUntilMs);
    bool allowMicSend = !otaUpdateActive && (!mqttNetworkBusy || recording) && micActive() && audioIdle() &&
                        (recording || (cfg.hotwordStreamEnabled && waitingToWake &&
                                       !hotwordBackpressurePaused));
    if (wsReady) {
      int micFlushResult = ws.flushPendingMicFrame();
      if (micFlushResult < 0) {
        ws.disconnect();
        wsBackpressureSinceMs = 0;
      } else if (ws.hasPendingMicFrame()) {
        if (!wsBackpressureSinceMs) wsBackpressureSinceMs = millis();
        if (millis() - wsBackpressureSinceMs >= 2000) {
          if (!recording && !ws.pendingMicFrameStarted()) {
            ws.resetPendingMicFrame();
            if (micQueue) xQueueReset(micQueue);
            hotwordBackpressureUntilMs = millis() + 10000;
            UART_LOGF("Hotword stream tạm nghỉ 10 giây vì TCP đầy; giữ WebSocket và ưu tiên WebUI. free=%u max_block=%u",
                      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
          } else {
            UART_LOGF("WebSocket có frame mic gửi dở quá 2 giây; đóng socket. recording=%d free=%u max_block=%u",
                      recording ? 1 : 0, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
            wsForceReconnectAtMs = millis() + WS_RECONNECT_INTERVAL_MS;
            ws.disconnect();
          }
          wsBackpressureSinceMs = 0;
        }
      } else if (allowMicSend) {
        wsBackpressureSinceMs = 0;
        if (xQueueReceive(micQueue, &wsSendFrame, 0) == pdTRUE &&
            !ws.queueMicFrame(wsSendFrame.data, FRAME_BYTES)) {
          UART_LOGF("Không thể xếp frame mic vào WebSocket; đóng socket. recording=%d free=%u",
                    recording ? 1 : 0, (unsigned)ESP.getFreeHeap());
          wsForceReconnectAtMs = millis() + WS_RECONNECT_INTERVAL_MS;
          ws.disconnect();
        }
      }

      /*
      if (cfg.serialLogs && millis() - lastMicLog > 1000) {
        LOG("Mic frame sent");
        lastMicLog = millis();
      }
      */

    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

static int argInt(AsyncWebServerRequest *request, const char *name, int fallback) {
  return request->hasParam(name, true) ? request->getParam(name, true)->value().toInt() : fallback;
}

static String argStr(AsyncWebServerRequest *request, const char *name, const char *fallback) {
  return request->hasParam(name, true) ? request->getParam(name, true)->value() : String(fallback);
}

static String fsNormalizePath(String path) {
  path.trim();
  path.replace("\\", "/");
  if (!path.length() || path[0] != '/') path = "/" + path;
  while (path.indexOf("//") >= 0) path.replace("//", "/");
  return path;
}

static bool fsPathIsProtected(const String &path) {
  return path == "/" ||
         path == "/index.html" || path == "/index.html.gz" ||
         path == "/app.js" || path == "/app.js.gz" ||
         path == "/app.css" || path == "/app.css.gz" ||
         path == "/busy.html" || path == "/busy.html.gz" ||
         path == "/vendor" || path.startsWith("/vendor/");
}

static void fsAppendJsonFile(JsonArray &files, const String &path, size_t size, bool directory) {
  JsonObject item = files.add<JsonObject>();
  item["path"] = path;
  item["name"] = path == "/" ? "/" : path.substring(path.lastIndexOf('/') + 1);
  item["size"] = size;
  item["directory"] = directory;
  item["protected"] = fsPathIsProtected(path);
}

static void fsListDir(JsonArray &files, const char *dirname, uint8_t levels) {
  File root = LittleFS.open(dirname);
  if (!root) return;

  if (!root.isDirectory()) {
    fsAppendJsonFile(files, String(dirname), root.size(), false);
    root.close();
    return;
  }

  File file = root.openNextFile();
  while (file) {
    String path = file.path();
    bool directory = file.isDirectory();
    fsAppendJsonFile(files, path, directory ? 0 : file.size(), directory);
    if (directory && levels > 0) fsListDir(files, path.c_str(), levels - 1);
    file.close();
    file = root.openNextFile();
  }
  root.close();
}

static bool fsDeleteDirRecursive(const String &path) {
  File root = LittleFS.open(path);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return false;
  }

  File file = root.openNextFile();
  while (file) {
    String childPath = file.path();
    bool directory = file.isDirectory();
    file.close();
    bool ok = directory ? fsDeleteDirRecursive(childPath) : LittleFS.remove(childPath);
    if (!ok) {
      root.close();
      return false;
    }
    file = root.openNextFile();
  }
  root.close();
  return LittleFS.rmdir(path);
}

static String buildLittleFsInfoJson() {
  JsonDocument doc;
  doc["success"] = true;
  doc["total_bytes"] = LittleFS.totalBytes();
  doc["used_bytes"] = LittleFS.usedBytes();
  doc["free_bytes"] = LittleFS.totalBytes() > LittleFS.usedBytes() ? LittleFS.totalBytes() - LittleFS.usedBytes() : 0;
  JsonArray files = doc["files"].to<JsonArray>();
  fsListDir(files, "/", 8);
  String out;
  serializeJson(doc, out);
  return out;
}

static void appendGpioConflict(String &message, const char *aName, int aPin, const char *bName, int bPin) {
  if (aPin < 0 || bPin < 0 || aPin != bPin) return;
  if (message.length()) message += "\n";
  message += aName;
  message += " GPIO";
  message += aPin;
  message += " trùng với ";
  message += bName;
  message += " GPIO";
  message += bPin;
}

static void appendGpioError(String &message, const char *name, int pin, const char *reason) {
  if (message.length()) message += "\n";
  message += name;
  message += " GPIO";
  message += pin;
  message += ": ";
  message += reason;
}

static bool gpioIsValidForChip(int pin) {
  if (pin < 0) return false;
  if (chipIsEsp32S3()) {
    return pin <= 48 && !(pin >= 22 && pin <= 25) && !(pin >= 26 && pin <= 37);
  }
  // GPIO6..11 are normally wired to the module SPI flash.
  return pin <= 39 && pin != 20 && pin != 24 && !(pin >= 6 && pin <= 11);
}

static bool gpioIsBootStrap(int pin) {
  if (chipIsEsp32S3()) return pin == 0 || pin == 3 || pin == 45 || pin == 46;
  return pin == 0 || pin == 2 || pin == 5 || pin == 12 || pin == 15;
}

static bool gpioCanOutput(int pin) {
  if (!gpioIsValidForChip(pin)) return false;
  if (!chipIsEsp32S3() && pin >= 34 && pin <= 39) return false;
  if (chipIsEsp32S3() && pin == 46) return false;
  return true;
}

static bool gpioCanInputPullup(int pin) {
  return gpioCanOutput(pin);
}

static bool gpioCanInput(int pin) {
  return gpioIsValidForChip(pin);
}

static String validateHardwareGpioPins(
  int pinLed, int pinBtnMic, int pinBtnVolUp, int pinBtnVolDown, int pinBtnWake,
  int pinRotaryClk, int pinRotaryDt, int pinRotarySw,
  int micBclk, int micWs, int micDin, int dacBclk, int dacLrc, int dacDout
) {
  struct PinItem { const char *name; int pin; };
  const PinItem pins[] = {
    {"LED WS2812", pinLed},
    {"Button Mic", pinBtnMic},
    {"Button Volume+", pinBtnVolUp},
    {"Button Volume-", pinBtnVolDown},
    {"Button WakeUP", pinBtnWake},
    {"Rotary CLK", pinRotaryClk},
    {"Rotary DT", pinRotaryDt},
    {"Rotary SW", pinRotarySw},
    {"I2S Mic BCLK", micBclk},
    {"I2S Mic WS/LRCL", micWs},
    {"I2S Mic DOUT", micDin},
    {"I2S DAC BCLK", dacBclk},
    {"I2S DAC LRC", dacLrc},
    {"I2S DAC DIN", dacDout}
  };
  String conflicts;
  for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
    if (pins[i].pin >= 0 && !gpioIsValidForChip(pins[i].pin)) {
      appendGpioError(conflicts, pins[i].name, pins[i].pin, "Không hợp lệ với chip hiện tại");
    }
  }
  for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
    if (pins[i].pin >= 0 && gpioIsValidForChip(pins[i].pin) && gpioIsBootStrap(pins[i].pin)) {
      appendGpioError(conflicts, pins[i].name, pins[i].pin,
                      "GPIO boot strap; ngoai vi co the lam ESP khong boot neu keo sai muc khi reset");
    }
  }
  const PinItem outputPins[] = {
    {"LED WS2812", pinLed},
    {"I2S Mic BCLK", micBclk},
    {"I2S Mic WS/LRCL", micWs},
    {"I2S DAC BCLK", dacBclk},
    {"I2S DAC LRC", dacLrc},
    {"I2S DAC DIN", dacDout}
  };
  for (size_t i = 0; i < sizeof(outputPins) / sizeof(outputPins[0]); i++) {
    if (outputPins[i].pin >= 0 && !gpioCanOutput(outputPins[i].pin)) {
      appendGpioError(conflicts, outputPins[i].name, outputPins[i].pin, "Không hỗ trợ output");
    }
  }
  if (micDin >= 0 && !gpioCanInput(micDin)) {
    appendGpioError(conflicts, "I2S Mic DOUT", micDin, "Khong ho tro input");
  }
  if (pinLed >= 0 && gpioIsValidForChip(pinLed) && gpioCanOutput(pinLed) && !gpioSupportsFastLed(pinLed)) {
    const int candidates[] = {0, 2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 27, 32, 33, 48};
    String available;
    for (int candidate : candidates) {
      if (!gpioIsValidForChip(candidate) || gpioIsBootStrap(candidate) || !gpioCanOutput(candidate) || !gpioSupportsFastLed(candidate)) continue;
      bool occupied = false;
      // Bỏ qua LED hiện tại (pins[0]); loại mọi GPIO đã cấp cho chức năng khác.
      for (size_t i = 1; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        if (pins[i].pin == candidate) {
          occupied = true;
          break;
        }
      }
      if (occupied) continue;
      if (available.length()) available += ", ";
      available += "GPIO";
      available += candidate;
    }
    String reason = "FastLED không hỗ trợ GPIO này trong firmware. GPIO được hỗ trợ và chưa được cấu hình: ";
    reason += available.length() ? available : "không còn GPIO phù hợp";
    appendGpioError(conflicts, "LED WS2812", pinLed, reason.c_str());
  }
  const PinItem pullupPins[] = {
    {"Button Mic", pinBtnMic},
    {"Button Volume+", pinBtnVolUp},
    {"Button Volume-", pinBtnVolDown},
    {"Button WakeUP", pinBtnWake},
    {"Rotary CLK", pinRotaryClk},
    {"Rotary DT", pinRotaryDt},
    {"Rotary SW", pinRotarySw}
  };
  for (size_t i = 0; i < sizeof(pullupPins) / sizeof(pullupPins[0]); i++) {
    if (pullupPins[i].pin >= 0 && !gpioCanInputPullup(pullupPins[i].pin)) {
      appendGpioError(conflicts, pullupPins[i].name, pullupPins[i].pin, "Không hỗ trợ INPUT_PULLUP");
    }
  }
  for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
    for (size_t j = i + 1; j < sizeof(pins) / sizeof(pins[0]); j++) {
      appendGpioConflict(conflicts, pins[i].name, pins[i].pin, pins[j].name, pins[j].pin);
    }
  }
  return conflicts;
}

static void addChipInfo(JsonDocument &doc) {
  doc["chip"]["model"] = ESP.getChipModel();
  doc["chip"]["build_target"] = buildTarget();
  doc["chip"]["update_family"] = updateFamily();
  doc["chip"]["revision"] = ESP.getChipRevision();
  doc["chip"]["cores"] = ESP.getChipCores();
  doc["chip"]["flash_size_mb"] = (float)ESP.getFlashChipSize() / (1024.0f * 1024.0f);
  doc["chip"]["psram_active"] = psramFound();
  doc["chip"]["psram_capacity_mb"] = psramFound() ? (float)ESP.getPsramSize() / (1024.0f * 1024.0f) : 0;
}

static void addTaskStatus(JsonObject tasks, const char *name, TaskHandle_t handle, uint32_t configuredStack) {
  JsonObject task = tasks[name].to<JsonObject>();
  task["stack_configured"] = configuredStack;
  task["stack_high_water"] = handle ? uxTaskGetStackHighWaterMark(handle) : 0;
}

static bool fetchHttpText(const char *url, String &body, int &httpCode, String &error) {
  // mbedTLS cần một khối heap liên tục khá lớn. Từ chối sớm thay vì để quá
  // trình handshake làm phân mảnh heap hoặc khiến WebUI/AsyncTCP mất ổn định.
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t largestBlock = ESP.getMaxAllocHeap();
  if (freeHeap < 70000U || largestBlock < 40000U) {
    error = "Không đủ bộ nhớ cho HTTPS (heap=" + String(freeHeap) +
            ", largest=" + String(largestBlock) + ")";
    httpCode = 0;
    LOGF("Bỏ qua HTTPS do bộ nhớ thấp: free=%u, largest=%u",
         (unsigned)freeHeap, (unsigned)largestBlock);
    return false;
  }
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);
  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.useHTTP10(true);
  if (!http.begin(client, url)) {
    error = "Không thể khởi tạo HTTPClient";
    httpCode = 0;
    return false;
  }
  http.addHeader("User-Agent", "VBot-ESP32-Client");
  httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    error = http.errorToString(httpCode);
    http.end();
    return false;
  }
  body = http.getString();
  http.end();
  return true;
}

static String buildUpdateCheckJson() {
  JsonDocument doc;
  doc["success"] = false;
  doc["chip_model"] = ESP.getChipModel();
  doc["chip_target"] = buildTarget();
  doc["update_family"] = updateFamily();
  doc["current_build_date"] = VBOT_CLIENT_VERSION;
  doc["version_url"] = updateVersionUrl();
  doc["version_fallback_url"] = updateVersionFallbackUrl();
  doc["bin_url"] = updateBinUrl();
  doc["littlefs_bin_url"] = updateLittleFsBinUrl();

  if (!WiFi.isConnected()) {
    doc["message"] = "WiFi chưa kết nối";
    String out;
    serializeJson(doc, out);
    return out;
  }

  String fetchedBody;
  int fetchedCode = 0;
  String fetchedError;
  const char *usedUrl = updateVersionUrl();
  if (!fetchHttpText(usedUrl, fetchedBody, fetchedCode, fetchedError)) {
    doc["http_code"] = fetchedCode;
    doc["http_error"] = fetchedError;
    usedUrl = updateVersionFallbackUrl();
    if (!fetchHttpText(usedUrl, fetchedBody, fetchedCode, fetchedError)) {
      doc["fallback_http_code"] = fetchedCode;
      doc["fallback_http_error"] = fetchedError;
      doc["message"] = "Không tải được file Version.json";
      String out;
      serializeJson(doc, out);
      return out;
    }
  }
  doc["used_version_url"] = usedUrl;
  JsonDocument fetchedRemote;
  DeserializationError fetchedErr = deserializeJson(fetchedRemote, fetchedBody);
  if (fetchedErr) {
    doc["message"] = "File Version.json không hợp lệ";
    doc["error"] = fetchedErr.c_str();
    String out;
    serializeJson(doc, out);
    return out;
  }
  const char *fetchedBuildDate = fetchedRemote["build_date"] | "";
  const char *fetchedDescription = fetchedRemote["description"] | "";
  bool fetchedHasNewVersion = strcmp(fetchedBuildDate, VBOT_CLIENT_VERSION) != 0;
  doc["success"] = true;
  doc["remote_build_date"] = fetchedBuildDate;
  doc["description"] = fetchedDescription;
  doc["has_new_version"] = fetchedHasNewVersion;
  doc["message"] = fetchedHasNewVersion ? "Có phiên bản mới" : "Đang dùng phiên bản mới nhất";
  String fetchedOut;
  serializeJson(doc, fetchedOut);
  return fetchedOut;
}

static void updateCheckTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    String result = buildUpdateCheckJson();
    xSemaphoreTake(updateMutex, portMAX_DELAY);
    updateCheckResult = result;
    updateCheckRunning = false;
    updateCheckReady = true;
    xSemaphoreGive(updateMutex);
  }
}

static String updatePendingJson() {
  return "{\"success\":true,\"pending\":true,\"message\":\"Dang kiem tra cap nhat\"}";
}

static String buildWebUiStateJson(const char *&failureReason) {
  failureReason = "config_locked";
  JsonDocument doc;
  // Never wait for a config save in the AsyncTCP callback.
  if (cfgMutex && xSemaphoreTake(cfgMutex, 0) != pdTRUE) return String();
  doc["success"] = true;
  fillConfigJson(doc["config"].to<JsonObject>());
  doc["status"]["client_name"] = cfg.clientName;
  doc["status"]["version"] = VBOT_CLIENT_VERSION;
  doc["status"]["startup_mode"] = cfg.startupMode;
  doc["status"]["ip"] = WiFi.localIP().toString();
  doc["status"]["wifi_connected"] = WiFi.isConnected();
  doc["status"]["wifi_ssid"] = WiFi.isConnected() ? WiFi.SSID() : "";
  doc["status"]["rssi"] = WiFi.isConnected() ? WiFi.RSSI() : 0;
  doc["status"]["websocket_connected"] = (bool)wsReady;
  doc["status"]["mqtt_enabled"] = cfg.mqttEnabled;
  doc["status"]["mqtt_connected"] = mqttClient.connected();
  doc["status"]["hardware_gpio_valid"] = (bool)hardwareGpioValid;
  doc["status"]["hardware_gpio_errors"] = hardwareGpioErrors;
  doc["status"]["socket"] = socketUrl();
  doc["status"]["chip_model"] = ESP.getChipModel();
  doc["status"]["chip_revision"] = ESP.getChipRevision();
  doc["status"]["chip_cores"] = ESP.getChipCores();
  doc["status"]["chip_target"] = buildTarget();
  doc["status"]["update_family"] = updateFamily();
  doc["status"]["uptime_ms"] = millis();
  doc["status"]["free_heap"] = ESP.getFreeHeap();
  doc["status"]["min_free_heap"] = ESP.getMinFreeHeap();
  doc["status"]["flash_size"] = ESP.getFlashChipSize();
  doc["status"]["sketch_size"] = ESP.getSketchSize();
  doc["status"]["free_sketch_space"] = ESP.getFreeSketchSpace();
  doc["status"]["littlefs_total"] = LittleFS.totalBytes();
  doc["status"]["littlefs_used"] = LittleFS.usedBytes();
  JsonObject tasks = doc["status"]["tasks"].to<JsonObject>();
  addTaskStatus(tasks, "mic", micTaskHandle, MIC_TASK_STACK);
  addTaskStatus(tasks, "websocket", websocketTaskHandle, WEBSOCKET_TASK_STACK);
  addTaskStatus(tasks, "dac", dacTaskHandle, DAC_TASK_STACK);
  addTaskStatus(tasks, "led", ledTaskHandle, LED_TASK_STACK);
  addTaskStatus(tasks, "button", buttonTaskHandle, BUTTON_TASK_STACK);
  addTaskStatus(tasks, "update", updateTaskHandle, UPDATE_TASK_STACK);
  addTaskStatus(tasks, "wifi", wifiTaskHandle, WIFI_TASK_STACK);
  doc["update"]["version_url"] = updateVersionUrl();
  doc["update"]["version_fallback_url"] = updateVersionFallbackUrl();
  doc["update"]["bin_url"] = updateBinUrl();
  doc["update"]["littlefs_bin_url"] = updateLittleFsBinUrl();
  unlockCfg();
  if (doc.overflowed()) {
    failureReason = "json_allocation_failed";
    UART_LOGF("WebUI JSON: thiếu heap khi tạo dữ liệu; free=%u max_block=%u",
              (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    return String();
  }
  const size_t expectedLength = measureJson(doc);
  String out;
  failureReason = "json_serialization_failed";
  if (!out.reserve(expectedLength) || serializeJson(doc, out) != expectedLength || out.length() != expectedLength) {
    UART_LOGF("WebUI JSON: không đủ bộ nhớ cho %u byte; free=%u max_block=%u",
              (unsigned)expectedLength, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    return String();
  }
  return out;
}

static String requestAudioUrl(AsyncWebServerRequest *request) {
  for (const char *name : {"url", "audio_url"}) {
    if (request->hasParam(name, true)) return request->getParam(name, true)->value();
    if (request->hasParam(name)) return request->getParam(name)->value();
  }
  return "";
}

static void handlePlayUrlRequest(AsyncWebServerRequest *request) {
  if (!hardwareGpioValid) {
    request->send(409, CONTENT_JSON, "{\"success\":false,\"message\":\"GPIO dang o che do an toan; audio da bi vo hieu hoa\"}");
    return;
  }
  String url = requestAudioUrl(request);
  url.trim();
  if (!validAudioUrl(url.c_str())) {
    request->send(400, CONTENT_JSON, "{\"success\":false,\"message\":\"URL âm thanh không hợp lệ\"}");
    return;
  }
  lockMediaState();
  currentMqttMediaTitle = "";
  currentMqttMediaCover = "";
  unlockMediaState();
  if (!playMp3Url(url.c_str())) {
    request->send(503, CONTENT_JSON, "{\"success\":false,\"message\":\"Không thể đưa âm thanh vào hàng đợi\"}");
    return;
  }
  JsonDocument response;
  response["success"] = true;
  response["message"] = isDirectHttpAudioFileUrl(url)
    ? "Đang phát trực tiếp file âm thanh HTTP"
    : "Đang phát URL âm thanh qua audio_proxy";
  response["url"] = url;
  String body;
  serializeJson(response, body);
  request->send(200, CONTENT_JSON, body);
}

static bool webUiWorkBusy() {
  return recording || !audioIdle() || otaUpdateActive || fsMaintenanceActive ||
         (audioQueue && uxQueueMessagesWaiting(audioQueue) > 0);
}

static const char *webUiBusyReason() {
  if (otaUpdateActive) return "ota_active";
  if (fsMaintenanceActive) return "filesystem_maintenance";
  if (recording) return "recording";
  if (!audioIdle()) return "audio_playing";
  return "audio_queued";
}

static void sendWebUiUnavailable(AsyncWebServerRequest *request, const char *reason, int code = 503) {
  const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  const unsigned freeBytes = heap_caps_get_free_size(caps);
  const unsigned largestBlock = heap_caps_get_largest_free_block(caps);
  const char *message = "Thiết bị đang xử lý tác vụ. Vui lòng thử lại sau vài giây.";
  if (!strcmp(reason, "rate_limited") || !strcmp(reason, "response_in_flight"))
    message = "Yêu cầu trước đang xử lý hoặc vừa hoàn tất. Vui lòng thử lại sau vài giây.";
  else if (!strcmp(reason, "config_locked"))
    message = "Cấu hình đang được sử dụng. Vui lòng thử lại.";
  else if (!strncmp(reason, "json_", 5) || !strcmp(reason, "response_allocation_failed"))
    message = "Không đủ bộ nhớ tạo trạng thái WebUI. Vui lòng thử lại.";
  // Fixed diagnostic buffer: no JSON document allocation on the failure path.
  char body[640];
  snprintf(body, sizeof(body),
    "{\"success\":false,\"message\":\"%s\",\"reason\":\"%s\",\"free_internal_heap\":%u,\"largest_internal_block\":%u}",
    message, reason, freeBytes, largestBlock);
  static uint32_t lastLogMs = 0;
  static bool logged = false;
  if (!logged || millis() - lastLogMs >= 3000) {
    logged = true;
    lastLogMs = millis();
    UART_LOGF("WebUI HTTP %d: reason=%s free_internal=%u largest_block=%u",
              code, reason, freeBytes, largestBlock);
  }
  AsyncWebServerResponse *response = request->beginResponse(code, CONTENT_JSON, body);
  response->addHeader("Cache-Control", "no-store");
  response->addHeader("Retry-After", "3");
  request->send(response);
}

static bool admitWebUiWork(AsyncWebServerRequest *request) {
  if (!webUiWorkBusy()) return true;
  sendWebUiUnavailable(request, webUiBusyReason());
  return false;
}

static std::atomic<bool> webUiStateInFlight{false};

// Own the serialized String until the response is destroyed (including disconnect).
// Moving it avoids the second full JSON copy made by AsyncBasicResponse.
class WebUiStateResponse final : public AsyncAbstractResponse {
  String body;
  size_t offset = 0;
public:
  explicit WebUiStateResponse(String &&value) : body(std::move(value)) {
    _code = 200;
    _contentType = CONTENT_JSON;
    _contentLength = body.length();
    addHeader("Cache-Control", "no-store");
  }
  ~WebUiStateResponse() override { webUiStateInFlight.store(false); }
  bool _sourceValid() const override { return true; }
  size_t _fillBuffer(uint8_t *buffer, size_t maxLen) override {
    const size_t count = min(maxLen, body.length() - offset);
    memcpy(buffer, body.c_str() + offset, count);
    offset += count;
    return count;
  }
};

static void sendWebUiState(AsyncWebServerRequest *request) {
  if (!admitWebUiWork(request)) return;
  static uint32_t lastBuildMs = 0;
  static bool builtOnce = false;
  const uint32_t now = millis();
  if (builtOnce && now - lastBuildMs < 1500) {
    sendWebUiUnavailable(request, "rate_limited", 429);
    return;
  }
  if (webUiStateInFlight.exchange(true)) {
    sendWebUiUnavailable(request, "response_in_flight", 429);
    return;
  }
  builtOnce = true;
  lastBuildMs = now;
  const char *failureReason = nullptr;
  String body = buildWebUiStateJson(failureReason);
  if (body.isEmpty()) {
    webUiStateInFlight.store(false);
    sendWebUiUnavailable(request, failureReason);
    return;
  }
  auto *response = new (std::nothrow) WebUiStateResponse(std::move(body));
  if (!response) {
    webUiStateInFlight.store(false);
    sendWebUiUnavailable(request, "response_allocation_failed");
    return;
  }
  request->send(response);
}

static void initWeb() {
  web.on("/api/webui_state", HTTP_GET, sendWebUiState);
  web.on("/webui_state", HTTP_GET, sendWebUiState);
  // Không dùng serveStatic cho cả thư mục: handler này mặc định dò index.htm và
  // tạo thêm ETag/header động. Trên ESP32 không PSRAM, request /webui/ hoặc file
  // không tồn tại có thể làm phân mảnh heap rồi std::bad_alloc/abort.
  web.on("/webui", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
  web.on("/webui/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
  web.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    // The gzipped page is streamed from flash; it does not need a large JSON
    // allocation. Do not reject it based on a guessed contiguous-heap threshold.
    if (otaUpdateActive || fsMaintenanceActive) {
      sendWebUiUnavailable(request, webUiBusyReason());
      return;
    }
    if (webUiWorkBusy()) {
      if (littleFsFileExists("/busy.html")) {
        sendLittleFsFile(request, "/busy.html", CONTENT_HTML, false);
      } else {
        request->send(503, CONTENT_TEXT, "Trang hiển thị trạng thái bận của WebUI bị thiếu. Vui lòng nạp lại file littlefs.bin");
      }
      return;
    }
    if (littleFsFileExists("/index.html")) {
      sendLittleFsFile(request, "/index.html", CONTENT_HTML);
    } else {
      request->send(503, CONTENT_TEXT, "Các tệp cấu hình WebUI bị thiếu. Vui lòng nạp lại file littlefs.bin");
    }
  });
  web.on("/download_config", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!admitWebUiWork(request)) return;
    JsonDocument doc;
    lockCfg();
    fillConfigJson(doc.to<JsonObject>());
    String clientName = cfg.clientName;
    unlockCfg();
    String out;
    serializeJsonPretty(doc, out);
    AsyncWebServerResponse *response = request->beginResponse(200, CONTENT_JSON, out);
    String filename = sanitizeIdentifier(clientName) + "_VBot_Client_NVS_Config.json";
    response->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    request->send(response);
  });
  web.on(
    "/restore_config",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      RestoreUploadContext *upload = static_cast<RestoreUploadContext *>(request->_tempObject);
      request->_tempObject = nullptr;
      if (!upload) {
        request->send(400, CONTENT_JSON, "{\"success\":false,\"message\":\"Khong nhan duoc tep cau hinh\"}");
        return;
      }
      if (upload->tooLarge) {
        free(upload);
        request->send(413, CONTENT_JSON, "{\"success\":false,\"message\":\"Tep cau hinh vuot qua 8192 byte\"}");
        return;
      }
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, upload->data, upload->length);
      free(upload);
      if (err) {
        request->send(400, CONTENT_JSON, "{\"success\":false,\"message\":\"Cấu hình JSON không hợp lệ\"}");
        return;
      }
      lockCfg();
      bool applied = applyConfigJson(doc);
      unlockCfg();
      if (!applied) {
        request->send(400, CONTENT_JSON, "{\"success\":false,\"message\":\"GPIO trong cấu hình bị trùng hoặc không hợp lệ. Cấu hình không được khôi phục.\"}");
        return;
      }
      request->send(200, CONTENT_JSON, "{\"success\":true,\"message\":\"Cấu hình đã được khôi phục. Đang khởi động lại.\"}");
      scheduleRestart(700, "Khôi phục cấu hình từ WebUI");
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if (index == 0) {
        request->_tempObject = calloc(1, sizeof(RestoreUploadContext));
      }
      RestoreUploadContext *upload = static_cast<RestoreUploadContext *>(request->_tempObject);
      if (upload) {
        if (index + len > 8192) {
          upload->tooLarge = true;
        } else if (!upload->tooLarge) {
          memcpy(upload->data + index, data, len);
          upload->length = index + len;
          upload->data[upload->length] = '\0';
        }
      }
      if (final) LOGF("Tải lên bản sao lưu khôi phục cấu hình: %s bytes=%u", filename.c_str(), (unsigned)(index + len));
    }
  );
  web.on("/Check_PS_RAM", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool active = psramFound();
    JsonDocument doc;
    doc["psram_active"] = active;
    doc["psram_capacity_mb"] = active ? (float)ESP.getPsramSize() / (1024.0f * 1024.0f) : 0;
    doc["chip_suffix"] = active ? "R" : "";
    String out;
    serializeJson(doc, out);
    request->send(200, CONTENT_JSON, out);
  });
  web.on("/test_led", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!hardwareGpioValid || !cfg.ledEnabled || !leds) {
      request->send(409, CONTENT_JSON, "{\"success\":false,\"message\":\"LED khong san sang hoac GPIO dang o che do an toan\"}");
      return;
    }
    String effectName = request->hasParam("effect", true) ? request->getParam("effect", true)->value() : "LED_STARTUP";
    LedEffect effect;
    if (!ledEffectFromString(effectName, effect)) {
      request->send(400, CONTENT_JSON, "{\"success\":false,\"message\":\"Hiệu ứng LED không hợp lệ\"}");
      return;
    }
    setLedEffect(effect);
    testLedUntilMs = millis() + 3000;
    String out = "{\"success\":true,\"effect\":\"" + effectName + "\"}";
    request->send(200, CONTENT_JSON, out);
  });
  web.on("/check_update", HTTP_POST, [](AsyncWebServerRequest *request) {
    String response;
    int status = 202;
    bool startCheck = false;
    xSemaphoreTake(updateMutex, portMAX_DELAY);
    if (updateCheckReady) {
      response = updateCheckResult;
      updateCheckReady = false;
      status = 200;
    } else {
      response = updatePendingJson();
      if (!updateCheckRunning) {
        updateCheckRunning = true;
        startCheck = true;
      }
    }
    xSemaphoreGive(updateMutex);
    if (startCheck && updateTaskHandle) xTaskNotifyGive(updateTaskHandle);
    request->send(status, CONTENT_JSON, response);
  });
  web.on("/api/littlefs", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!admitWebUiWork(request)) return;
    request->send(200, CONTENT_JSON, buildLittleFsInfoJson());
  });
  web.on("/littlefs_list", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!admitWebUiWork(request)) return;
    request->send(200, CONTENT_JSON, buildLittleFsInfoJson());
  });
  web.on("/littlefs_format", HTTP_POST, [](AsyncWebServerRequest *request) {
    fsMaintenanceActive = true;
    if (!audioIdle() || audioQueue == nullptr || uxQueueMessagesWaiting(audioQueue) > 0) {
      fsMaintenanceActive = false;
      request->send(409, CONTENT_JSON, "{\"success\":false,\"message\":\"Khong the format LittleFS khi audio dang phat hoac dang cho\"}");
      return;
    }
    bool ok = LittleFS.format();
    if (ok) refreshLocalSoundCache();
    fsMaintenanceActive = false;
    JsonDocument doc;
    doc["success"] = ok;
    doc["message"] = ok ? "Đã format LittleFS. Cần nạp lại giao diện WebUI bằng file littlefs.bin" : "Format LittleFS thất bại";
    String out;
    serializeJson(doc, out);
    request->send(ok ? 200 : 500, CONTENT_JSON, out);
  });
  web.on("/scan_wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!admitWebUiWork(request)) return;
    JsonDocument doc;
    int count = WiFi.scanComplete();
    if (count == WIFI_SCAN_FAILED) {
      WiFi.scanNetworks(true, true);
      doc["success"] = true;
      doc["scanning"] = true;
      doc["message"] = "Đang scan WiFi";
    } else if (count == WIFI_SCAN_RUNNING) {
      doc["success"] = true;
      doc["scanning"] = true;
      doc["message"] = "Đang scan WiFi";
    } else if (count < 0) {
      doc["success"] = false;
      doc["scanning"] = false;
      doc["message"] = "Không quét được mạng WiFi";
    } else {
      doc["success"] = true;
      doc["scanning"] = false;
      doc["count"] = count;
      JsonArray networks = doc["networks"].to<JsonArray>();
      for (int i = 0; i < count; i++) {
        JsonObject item = networks.add<JsonObject>();
        item["ssid"] = WiFi.SSID(i);
        item["rssi"] = WiFi.RSSI(i);
        item["channel"] = WiFi.channel(i);
        wifi_auth_mode_t enc = WiFi.encryptionType(i);
        item["encryption"] = enc == WIFI_AUTH_OPEN ? "🔓" : "🔒";
      }
      WiFi.scanDelete();
    }
    String out;
    serializeJson(doc, out);
    request->send(200, CONTENT_JSON, out);
  });
  web.on("/connect_wifi", HTTP_POST, [](AsyncWebServerRequest *request) {
    String ssid = request->hasParam("ssid", true) ? request->getParam("ssid", true)->value() : "";
    String password = request->hasParam("password", true) ? request->getParam("password", true)->value() : "";
    ssid.trim();
    if (!ssid.length()) {
      request->send(400, CONTENT_JSON, "{\"success\":false,\"message\":\"Tên mạng Wifi không được để trống\"}");
      return;
    }
    WiFi.persistent(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    JsonDocument doc;
    doc["success"] = true;
    doc["ssid"] = ssid;
    doc["message"] = "Đã lưu WiFi mới. ESP32 sẽ khởi động lại để kết nối.";
    String out;
    serializeJson(doc, out);
    request->send(200, CONTENT_JSON, out);
    scheduleRestart(1800, "Áp dụng WiFi mới từ WebUI");
  });
  web.on("/play_url", HTTP_POST, handlePlayUrlRequest);
  web.on("/api/play_url", HTTP_POST, handlePlayUrlRequest);
  web.on("/google_tts", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!hardwareGpioValid) {
      sendCorsJsonResult(request, 409, false, "GPIO dang o che do an toan; audio da bi vo hieu hoa");
      return;
    }
    String text = request->hasParam("text", true) ? request->getParam("text", true)->value() : "";
    text.trim();
    if (!text.length()) {
      sendCorsJsonResult(request, 400, false, "Nội dung TTS không được để trống");
      return;
    }
    if (text.length() > GOOGLE_TTS_MAX_TEXT_BYTES) {
      sendCorsJsonResult(request, 400, false, "Nội dung TTS vượt quá 240 byte UTF-8");
      return;
    }
    if (!queueGoogleTranslateTts(text)) {
      sendCorsJsonResult(request, 503, false, "Không thể đưa Google TTS vào hàng đợi");
      return;
    }
    sendCorsJsonResult(request, 200, true, "Đang phát Google Translate TTS qua audio_proxy");
  });
  web.on("/stop_audio", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!stopAudioPlaybackWithFeedback(true)) {
      sendJsonResult(request, 503, false, "Không thể đưa lệnh dừng vào hàng đợi audio");
      return;
    }
    sendJsonResult(request, 200, true, "Đang dừng phát âm thanh");
  });
  web.on("/VBot_Client_Info", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    lockCfg();
    doc["success"] = true;
    doc["project_name"] = "VBot Client Offline Socket";
    doc["connection_protocol"] = "Socket";
    doc["version"] = VBOT_CLIENT_VERSION;
    doc["device"] = "VBot Client ESP32";
    doc["client_name"] = cfg.clientName;
    doc["mqtt_client_name"] = cfg.mqttClientName;
    addChipInfo(doc);
    doc["update"]["target"] = chipIsEsp32S3() ? "ESP32S3" : "ESP32";
    doc["update"]["version_url"] = updateVersionUrl();
    doc["update"]["version_fallback_url"] = updateVersionFallbackUrl();
    doc["update"]["bin_url"] = updateBinUrl();
    doc["update"]["littlefs_bin_url"] = updateLittleFsBinUrl();
    doc["ip"] = WiFi.localIP().toString();
    doc["mac"] = WiFi.macAddress();
    doc["wifi_connected"] = WiFi.isConnected();
    doc["wifi_ssid"] = WiFi.SSID();
    doc["rssi"] = WiFi.isConnected() ? WiFi.RSSI() : 0;
    doc["websocket_connected"] = (bool)wsReady;
    doc["recording"] = (bool)recording;
    doc["waiting_to_wake"] = (bool)waitingToWake;
    doc["pending_auto_continue"] = (bool)pendingAutoContinue;
    doc["active_session_id"] = activeSessionId;
    doc["configured_session_prefix"] = sessionPrefixFromClientName();
    doc["startup_mode"] = cfg.startupMode;
    doc["working_mode"] = cfg.workingMode;
    doc["server"]["host"] = cfg.serverHost;
    doc["server"]["port"] = cfg.serverPort;
    doc["server"]["api_port"] = cfg.apiPort;
    doc["server"]["path"] = cfg.serverPath;
    doc["server"]["socket"] = socketUrl();
    doc["server"]["stream_api_url"] = cfg.streamApiUrl;
    doc["server"]["use_custom_stream_api_url"] = cfg.useCustomStreamApiUrl;
    doc["server"]["fallback_stream_api_to_server"] = cfg.fallbackStreamApiToServer;
    doc["server"]["effective_stream_api_url"] = streamApiBaseUrl();
    //doc["server_socket"] = socketUrl();
    doc["audio"]["volume"] = cfg.volume;
    doc["audio"]["mic_enabled"] = cfg.micEnabled;
    doc["audio"]["mic_active"] = micActive();
    doc["audio"]["temporary_mic_enabled"] = (bool)temporaryMicEnabled;
    doc["audio"]["hotword_stream_enabled"] = cfg.hotwordStreamEnabled;
    doc["audio"]["conversation_mode"] = cfg.conversationMode;
    doc["audio"]["startup_sound_enabled"] = cfg.startupSoundEnabled;
    //doc["audio"]["current_url"] = currentAudioUrl;
    doc["audio"]["mic_gain"] = cfg.micGain;
    doc["logs"]["serial"] = cfg.serialLogs;
    doc["led"]["active"] = cfg.ledEnabled;
    doc["led"]["gpio"] = cfg.pinLed;
    doc["led"]["count"] = cfg.ledCount;
    doc["led"]["brightness_percent"] = cfg.ledBrightnessPct;
    doc["led"]["think_color"] = cfg.ledThinkColor;
    doc["led"]["mute_color"] = cfg.ledMuteColor;
    doc["gpio"]["button_mic"] = cfg.pinBtnMic;
    doc["gpio"]["button_volume_up"] = cfg.pinBtnVolUp;
    doc["gpio"]["button_volume_down"] = cfg.pinBtnVolDown;
    doc["gpio"]["button_wakeup"] = cfg.pinBtnWake;
    doc["gpio"]["rotary_clk"] = cfg.pinRotaryClk;
    doc["gpio"]["rotary_dt"] = cfg.pinRotaryDt;
    doc["gpio"]["rotary_sw"] = cfg.pinRotarySw;
    doc["button"]["active"] = cfg.buttonsEnabled;
    doc["button"]["input_mode"] = cfg.inputMode;
    doc["button"]["wakeup_long_action"] = cfg.wakeLongAction;
    doc["button"]["press_debounce_ms"] = cfg.buttonPressDebounceMs;
    doc["button"]["release_debounce_ms"] = cfg.buttonReleaseDebounceMs;
    doc["button"]["hold_ms"] = cfg.buttonHoldMs;
    doc["rotary"]["clk"] = cfg.pinRotaryClk;
    doc["rotary"]["dt"] = cfg.pinRotaryDt;
    doc["rotary"]["sw"] = cfg.pinRotarySw;
    doc["rotary"]["volume_step"] = cfg.rotaryVolumeStep;
    doc["i2s_mic"]["bclk"] = cfg.micBclk;
    doc["i2s_mic"]["ws"] = cfg.micWs;
    doc["i2s_mic"]["din"] = cfg.micDin;
    doc["i2s_mic"]["channel"] = cfg.micChannel;
    doc["i2s_dac"]["bclk"] = cfg.dacBclk;
    doc["i2s_dac"]["lrc"] = cfg.dacLrc;
    doc["i2s_dac"]["dout"] = cfg.dacDout;
    doc["runtime"]["uptime_ms"] = millis();
    doc["runtime"]["free_heap"] = ESP.getFreeHeap();
    doc["runtime"]["min_free_heap"] = ESP.getMinFreeHeap();
    unlockCfg();
    String out;
    serializeJson(doc, out);
    request->send(200, CONTENT_JSON, out);
  });
  web.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    lockCfg();
    // Không sửa cấu hình live khi các task đang chạy. Chỉ lưu bản staging vào
    // NVS rồi restart để toàn bộ task cùng nhận một snapshot cấu hình mới.
    AppConfig pendingCfg = ::cfg;
    AppConfig &cfg = pendingCfg;
    bool oldConversationMode = cfg.conversationMode;
    int newPinLed = argInt(request, "pled", cfg.pinLed);
    int newPinBtnMic = argInt(request, "bmic", cfg.pinBtnMic);
    int newPinBtnVolUp = argInt(request, "bup", cfg.pinBtnVolUp);
    int newPinBtnVolDown = argInt(request, "bdn", cfg.pinBtnVolDown);
    int newPinBtnWake = argInt(request, "bwake", cfg.pinBtnWake);
    String newStartupMode = argStr(request, "startupmode", cfg.startupMode);
    bool newVbotServerMode = newStartupMode != "standalone";
    String newInputMode = argStr(request, "inputmode", cfg.inputMode);
    bool newRotaryMode = newInputMode == "rotary";
    int newPinRotaryClk = argInt(request, "rclk", cfg.pinRotaryClk);
    int newPinRotaryDt = argInt(request, "rdt", cfg.pinRotaryDt);
    int newPinRotarySw = argInt(request, "rsw", cfg.pinRotarySw);
    bool newLedEnabled = request->hasParam("leden", true);
    bool newButtonsEnabled = request->hasParam("btnen", true);
    int newMicBclk = argInt(request, "mbclk", cfg.micBclk);
    int newMicWs = argInt(request, "mws", cfg.micWs);
    int newMicDin = argInt(request, "mdin", cfg.micDin);
    int newDacBclk = argInt(request, "dbclk", cfg.dacBclk);
    int newDacLrc = argInt(request, "dlrc", cfg.dacLrc);
    int newDacDout = argInt(request, "ddout", cfg.dacDout);
    String gpioConflicts = validateHardwareGpioPins(
      newLedEnabled ? newPinLed : -1,
      newButtonsEnabled && !newRotaryMode ? newPinBtnMic : -1,
      newButtonsEnabled && !newRotaryMode ? newPinBtnVolUp : -1,
      newButtonsEnabled && !newRotaryMode ? newPinBtnVolDown : -1,
      newButtonsEnabled && !newRotaryMode ? newPinBtnWake : -1,
      newButtonsEnabled && newRotaryMode ? newPinRotaryClk : -1,
      newButtonsEnabled && newRotaryMode ? newPinRotaryDt : -1,
      newButtonsEnabled && newRotaryMode ? newPinRotarySw : -1,
      newVbotServerMode ? newMicBclk : -1,
      newVbotServerMode ? newMicWs : -1,
      newVbotServerMode ? newMicDin : -1,
      newDacBclk, newDacLrc, newDacDout
    );
    if (gpioConflicts.length()) {
      unlockCfg();
      String message = "Chân kết nối GPIO bị trùng hoặc không hợp lệ, sẽ không lưu cấu hình:\n" + gpioConflicts;
      sendJsonResult(request, 400, false, message.c_str());
      return;
    }
    strlcpy(cfg.clientName, argStr(request, "cname", cfg.clientName).c_str(), sizeof(cfg.clientName));
    strlcpy(cfg.serverHost, argStr(request, "host", cfg.serverHost).c_str(), sizeof(cfg.serverHost));
    strlcpy(cfg.serverPath, argStr(request, "path", cfg.serverPath).c_str(), sizeof(cfg.serverPath));
    strlcpy(cfg.streamApiUrl, normalizeBaseUrl(argStr(request, "sapi", cfg.streamApiUrl).c_str()).c_str(), sizeof(cfg.streamApiUrl));
    cfg.useCustomStreamApiUrl = request->hasParam("usesapi", true);
    cfg.fallbackStreamApiToServer = request->hasParam("sfbapi", true);
    strlcpy(cfg.sessionId, sanitizeIdentifier(String(cfg.clientName)).c_str(), sizeof(cfg.sessionId));
    strlcpy(cfg.startupMode, newStartupMode == "standalone" ? "standalone" : "vbot_server", sizeof(cfg.startupMode));
    strlcpy(cfg.workingMode, argStr(request, "mode", cfg.workingMode).c_str(), sizeof(cfg.workingMode));
    cfg.serverPort = argInt(request, "port", cfg.serverPort);
    cfg.apiPort = constrain(argInt(request, "apiport", cfg.apiPort), 1, 65535);
    cfg.volume = constrain(argInt(request, "vol", cfg.volume), 0, 100);
    if (request->hasParam("micgain", true)) {
      cfg.micGain = constrain(request->getParam("micgain", true)->value().toFloat(), 0.1f, 16.0f);
    }
    if (request->hasParam("pcmboost", true)) {
      cfg.pcmVolumeBoost = constrain(request->getParam("pcmboost", true)->value().toFloat(), 0.1f, 16.0f);
    }
    cfg.ledBrightnessPct = constrain(argInt(request, "ledbr", cfg.ledBrightnessPct), 0, 100);
    cfg.ledCount = constrain(argInt(request, "ledcnt", cfg.ledCount), 1, 300);
    cfg.ledEnabled = newLedEnabled;
    cfg.buttonsEnabled = newButtonsEnabled;
    strlcpy(cfg.inputMode, newRotaryMode ? "rotary" : "button", sizeof(cfg.inputMode));
    normalizeHexColor(cfg.ledThinkColor, sizeof(cfg.ledThinkColor), argStr(request, "lthink", cfg.ledThinkColor).c_str(), "#0066FF");
    normalizeHexColor(cfg.ledMuteColor, sizeof(cfg.ledMuteColor), argStr(request, "lmute", cfg.ledMuteColor).c_str(), "#FF8C00");
    cfg.pinLed = newPinLed;
    cfg.pinBtnMic = newPinBtnMic;
    cfg.pinBtnVolUp = newPinBtnVolUp;
    cfg.pinBtnVolDown = newPinBtnVolDown;
    cfg.pinBtnWake = newPinBtnWake;
    cfg.pinRotaryClk = newPinRotaryClk;
    cfg.pinRotaryDt = newPinRotaryDt;
    cfg.pinRotarySw = newPinRotarySw;
    cfg.rotaryVolumeStep = constrain(argInt(request, "rstep", cfg.rotaryVolumeStep), 1, 20);
    cfg.buttonPressDebounceMs = constrain(argInt(request, "bdbp", cfg.buttonPressDebounceMs), 10, 1000);
    cfg.buttonReleaseDebounceMs = constrain(argInt(request, "bdbr", cfg.buttonReleaseDebounceMs), 10, 1000);
    cfg.buttonHoldMs = constrain(argInt(request, "bhold", cfg.buttonHoldMs), 300, 10000);
    cfg.micBclk = newMicBclk;
    cfg.micWs = newMicWs;
    cfg.micDin = newMicDin;
    if (request->hasParam("mch", true)) {
      String channel = request->getParam("mch", true)->value();
      strlcpy(cfg.micChannel, channel == "R" ? "R" : "L", sizeof(cfg.micChannel));
    }
    if (request->hasParam("wla", true)) {
      strlcpy(cfg.wakeLongAction, request->getParam("wla", true)->value().c_str(), sizeof(cfg.wakeLongAction));
    }
    cfg.dacBclk = newDacBclk;
    cfg.dacLrc = newDacLrc;
    cfg.dacDout = newDacDout;
    cfg.micEnabled = request->hasParam("micen", true);
    cfg.hotwordStreamEnabled = request->hasParam("hword", true);
    cfg.conversationMode = request->hasParam("conv", true);
    cfg.startupSoundEnabled = request->hasParam("bootsnd", true);
    cfg.serialLogs = request->hasParam("logs", true);
    cfg.mdnsEnabled = request->hasParam("mdns", true);
    cfg.mqttEnabled = request->hasParam("mqen", true);
    strlcpy(cfg.mqttHost, argStr(request, "mqhost", cfg.mqttHost).c_str(), sizeof(cfg.mqttHost));
    cfg.mqttPort = constrain(argInt(request, "mqport", cfg.mqttPort), 1, 65535);
    strlcpy(cfg.mqttUsername, argStr(request, "mquser", cfg.mqttUsername).c_str(), sizeof(cfg.mqttUsername));
    strlcpy(cfg.mqttPassword, argStr(request, "mqpass", cfg.mqttPassword).c_str(), sizeof(cfg.mqttPassword));
    strlcpy(cfg.mqttClientName, argStr(request, "mqclient", cfg.mqttClientName).c_str(), sizeof(cfg.mqttClientName));
    saveConfig(pendingCfg);
    bool conversationChanged = oldConversationMode != cfg.conversationMode;
    if (conversationChanged) {
      playMp3Url(cfg.conversationMode ? conversationModeOnFeedbackUrl().c_str() : conversationModeOffFeedbackUrl().c_str());
    }
    LOG("Cấu hình WebUI đã được lưu vào Config. Thiết bị sẽ khởi động lại để áp dụng cấu hình mới.");
    unlockCfg();
    sendJsonResult(request, 200, true, "Lưu cấu hình Config thành công. Thiết bị sẽ khởi động lại...");
    scheduleRestart(conversationChanged ? 2500 : 700, "Áp dụng cấu hình mới từ WebUI");
  });
  web.on("/resetwifi", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool accepted = beginWifiResetSequence(false);
    sendJsonResult(request, accepted ? 200 : 409, accepted,
                   accepted ? "Đã nhận yêu cầu reset WiFi. Đang khởi động lại..." : "Không thể reset WiFi khi OTA/LittleFS đang được cập nhật.");
  });
  web.on("/factoryreset", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool accepted = beginWifiResetSequence(true);
    sendJsonResult(request, accepted ? 200 : 409, accepted,
                   accepted ? "Đã nhận yêu cầu xóa NVS và WiFi. Đang khởi động lại..." : "Không thể factory reset khi OTA/LittleFS đang được cập nhật.");
  });
  web.on("/resetnvs", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool resetOk = resetNvsOnly();
    sendJsonResult(request, resetOk ? 200 : 500, resetOk,
                   resetOk ? "Cấu hình Config đã được xóa. Đang khởi động lại..." : "Xóa cấu hình NVS thất bại; không khởi động lại.");
    if (resetOk) scheduleRestart(700, "Reset NVS từ WebUI");
  });
  web.on("/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool accepted = scheduleRestart(500, "Lệnh restart từ WebUI");
    sendJsonResult(request, accepted ? 200 : 409, accepted,
                   accepted ? "Đang khởi động lại..." : "Không thể restart trong khi OTA đang chạy.");
  });
  ElegantOTA.setAutoReboot(false);
  ElegantOTA.onStart([]() {
    otaUpdateActive = true;
    fsMaintenanceActive = true;
    restartAtMs = 0;
    restartBlockedLogged = false;
    stopAudioPlayback(true);
    recording = false;
    recordingDeadlineMs = 0;
    if (micQueue) xQueueReset(micQueue);
    UART_LOG("OTA bắt đầu; đã hủy lịch restart cũ và tạm khóa reset/NVS/LittleFS.");
  });
  ElegantOTA.onProgress([](size_t current, size_t total) {
    static uint8_t lastPercent = 255;
    uint8_t percent = total ? (uint8_t)((current * 100U) / total) : 0;
    if (lastPercent == 255 || percent >= lastPercent + 10 || percent == 100) {
      lastPercent = percent;
      UART_LOGF("OTA: %u%% (%u/%u byte)", percent, (unsigned)current, (unsigned)total);
    }
  });
  ElegantOTA.onEnd([](bool success) {
    otaUpdateActive = false;
    fsMaintenanceActive = false;
    if (!wsReady) wsForceReconnectAtMs = millis() + WS_RECONNECT_INTERVAL_MS;
    if (success) {
      UART_LOG("OTA hoàn tất thành công; chuẩn bị khởi động lại.");
      scheduleRestart(2000, "OTA hoàn tất thành công");
    } else {
      UART_LOG("OTA THẤT BẠI; thiết bị không restart và firmware hiện tại được giữ lại.");
    }
  });
  ElegantOTA.begin(&web);
  web.begin();
}

static void applyVolumeDelta(int delta) {
  lockCfg();
  int next = constrain((int)cfg.volume + delta, 0, 100);
  if (next == cfg.volume) {
    unlockCfg();
    return;
  }
  cfg.volume = (uint8_t)next;
  uint8_t volume = cfg.volume;
  unlockCfg();
  volumeSaveValue = volume;
  volumeSaveAtMs = millis() + VOLUME_SAVE_DELAY_MS;
  volumeSavePending = true;
  volumeSaveFeedbackPending = true;
  mqttStateDirty = true;
  AudioCommand cmd = {};
  cmd.type = AUDIO_CMD_SET_VOLUME;
  queueAudioCommand(cmd);
  LOGF("Âm lượng: %u", cfg.volume);
  showVolumeOverlay(volume);
}

static void handleButton(ButtonRuntime &state, int pin, void (*shortAction)(), void (*longAction)()) {
  uint32_t now = millis();
  bool raw = digitalRead(pin) == LOW;
  if (raw != state.rawPressed) {
    state.rawPressed = raw;
    state.lastRawChangeMs = now;
  }

  if (raw != state.stablePressed) {
    uint16_t debounce = raw ? cfg.buttonPressDebounceMs : cfg.buttonReleaseDebounceMs;
    if (now - state.lastRawChangeMs >= debounce) {
      state.stablePressed = raw;
      if (raw) {
        state.pressStartMs = now;
        state.longFired = false;
      } else if (!state.longFired && shortAction) {
        shortAction();
      }
    }
  }

  if (state.stablePressed && !state.longFired && now - state.pressStartMs >= cfg.buttonHoldMs) {
    state.longFired = true;
    if (longAction) longAction();
  }
}

static void micShortPress() {
  lockCfg();
  cfg.micEnabled = !cfg.micEnabled;
  bool enabled = cfg.micEnabled;
  unlockCfg();
  temporaryMicEnabled = false;
  lockNvs();
  prefs.putBool("micen", enabled);
  unlockNvs();
  LOGF("Mic: %s", cfg.micEnabled ? "Đang Bật" : "Đang Tắt");
  if (enabled) {
    if (localMicOnAvailable) playMp3FilePath("/sound/mic_on.mp3");
  } else {
    if (localMicOffAvailable) playMp3FilePath("/sound/mic_off.mp3");
  }
  setLedEffect(micActive() ? LED_OFF : LED_MUTE);
}

static void micLongPress() {
  if (!audioIdle() || !waitingToWake || recording) {
    bool wasRecording = recording;
    pendingAutoContinue = false;
    recording = false;
    recordingDeadlineMs = 0;
    waitingToWake = true;
    if (wasRecording && wsReady) wsStopRequest = true;
    restoreTemporaryMic();
    stopAudioPlaybackWithFeedback();
    setLedEffect(micActive() ? LED_OFF : LED_MUTE);
    LOG("Phát hiện nhấn giữ mic: dừng âm thanh và quay lại trạng thái chờ được đánh thức.");
    return;
  }
  lockCfg();
  cfg.conversationMode = !cfg.conversationMode;
  bool conversationMode = cfg.conversationMode;
  unlockCfg();
  lockNvs();
  prefs.putBool("conv", conversationMode);
  unlockNvs();
  clientConfigSyncRequested = true;
  LOGF("Chế độ hội thoại: %s", cfg.conversationMode ? "Đang Bật" : "Đang Tắt");
  playMp3Url(conversationMode ? conversationModeOnFeedbackUrl().c_str() : conversationModeOffFeedbackUrl().c_str());
}

static void volUpShortPress() { applyVolumeDelta(10); }
static void volUpLongPress() { applyVolumeDelta(100); }
static void volDownShortPress() { applyVolumeDelta(-10); }
static void volDownLongPress() { applyVolumeDelta(-100); }
static void wakeShortPress() {
  if (!vbotServerStartupEnabled()) return;
  //LOG("Nhấn nút WakeUP / Rotary SW: yêu cầu đánh thức thủ công");
  pendingAutoContinue = false;
  if (!cfg.micEnabled) temporaryMicEnabled = true;
  stopAudioPlayback(true);
  wakeSkipRequest = true;
}
static void wakeLongPress() {
  if (otaUpdateActive || fsMaintenanceActive) {
    UART_LOG("Bỏ qua nhấn giữ WakeUP vì OTA/LittleFS đang được cập nhật; không reset hoặc restart.");
    return;
  }
  if (!strcmp(cfg.wakeLongAction, "restart")) {
    LOG("Phát hiện nhấn giữ nút WakeUP. Khởi động lại ESP");
    scheduleRestart(300, "Nhấn giữ nút WakeUP");
    return;
  }
  if (!strcmp(cfg.wakeLongAction, "clean_all")) {
    LOG("Phát hiện nhấn giữ nút WakeUP: Reset bộ nhớ NVS và WiFi");
    if (resetAllNvs()) scheduleRestart(300, "Nhấn giữ WakeUP: factory reset");
    else UART_LOG("Không restart vì factory reset từ nút WakeUP thất bại.");
    return;
  }
  if (!strcmp(cfg.wakeLongAction, "reset_wifi")) {
    LOG("Phát hiện nhấn giữ nút WakeUP: Reset WiFi");
    WiFiManager wm;
    wm.resetSettings();
    bool resetOk = !wm.getWiFiIsSaved();
    UART_LOGF("Reset WiFi từ nút WakeUP: %s", resetOk ? "thành công" : "THẤT BẠI");
    if (resetOk) scheduleRestart(300, "Nhấn giữ WakeUP: reset WiFi");
    else UART_LOG("Không restart vì reset WiFi từ nút WakeUP thất bại.");
    return;
  }
  if (!strcmp(cfg.wakeLongAction, "reset_nvs")) {
    LOG("Phát hiện nhấn giữ nút WakeUP: Reset bộ nhớ NVS");
    if (resetNvsOnly()) scheduleRestart(300, "Nhấn giữ WakeUP: reset NVS");
    else UART_LOG("Không restart vì reset NVS từ nút WakeUP thất bại.");
    return;
  }
  pendingAutoContinue = false;
  recording = false;
  waitingToWake = true;
  restoreTemporaryMic();
  stopAudioPlaybackWithFeedback();
  setLedEffect(micActive() ? LED_OFF : LED_MUTE);
  LOG("Phát hiện nhấn giữ nút WakeUP: dừng âm thanh đang phát");
}

static uint8_t readRotaryState() {
  return ((digitalRead(cfg.pinRotaryClk) == HIGH) ? 0x02 : 0x00) |
         ((digitalRead(cfg.pinRotaryDt) == HIGH) ? 0x01 : 0x00);
}

static void handleRotary(RotaryRuntime &state) {
  uint8_t current = readRotaryState();
  if (!state.initialized) {
    state.lastState = current;
    state.accumulator = 0;
    state.initialized = true;
    return;
  }
  if (current == state.lastState) return;

  uint8_t transition = (state.lastState << 2) | current;
  switch (transition) {
    case 0b1101:
    case 0b0100:
    case 0b0010:
    case 0b1011:
      state.accumulator++;
      break;
    case 0b1110:
    case 0b1000:
    case 0b0001:
    case 0b0111:
      state.accumulator--;
      break;
    default:
      state.accumulator = 0;
      break;
  }
  state.lastState = current;

  if (state.accumulator >= 4) {
    state.accumulator = 0;
    applyVolumeDelta(cfg.rotaryVolumeStep);
  } else if (state.accumulator <= -4) {
    state.accumulator = 0;
    applyVolumeDelta(-cfg.rotaryVolumeStep);
  }
}

static void buttonTask(void *) {
  ButtonRuntime micButton, upButton, downButton, wakeButton, rotarySwButton;
  RotaryRuntime rotary;
  for (;;) {
    if (cfg.buttonsEnabled) {
      if (inputModeRotary()) {
        handleRotary(rotary);
        handleButton(rotarySwButton, cfg.pinRotarySw, wakeShortPress, wakeLongPress);
      } else {
        rotary.initialized = false;
        handleButton(micButton, cfg.pinBtnMic, micShortPress, micLongPress);
        handleButton(upButton, cfg.pinBtnVolUp, volUpShortPress, volUpLongPress);
        handleButton(downButton, cfg.pinBtnVolDown, volDownShortPress, volDownLongPress);
        handleButton(wakeButton, cfg.pinBtnWake, wakeShortPress, wakeLongPress);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(inputModeRotary() ? 2 : 10));
  }
}

static void wifiPortalOnce() {
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  LOG("Khởi động chế độ phát wifi, tồn tại trong vòng 180s để cấu hình kết nối wifi mới...");
  web.end();
  String apName = configPortalSsid();
  wm.startConfigPortal(apName.c_str());
  web.begin();
}

static void wifiTask(void *) {
  for (;;) {
    if (WiFi.isConnected()) {
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }
    LOG("WiFi bị ngắt kết nối, đang thử kết nối lại trong 180 giây. Nếu không thành công sẽ mở chế độ phát WiFi.");
    uint32_t start = millis();
    while (!WiFi.isConnected() && millis() - start < 180000) {
      WiFi.reconnect();
      vTaskDelay(pdMS_TO_TICKS(5000));
    }
    if (!WiFi.isConnected()) {
      wifiPortalOnce();
      vTaskDelay(pdMS_TO_TICKS(60000));
    }
  }
}

static String mqttTopic(const char *suffix) {
  return String(cfg.mqttClientName) + "/" + suffix;
}

static bool mqttForcePublish = false;

static void clearMqttStateCache() {
  for (uint8_t i = 0; i < MQTT_STATE_CACHE_SIZE; ++i) {
    mqttStateCache[i].topic = "";
    mqttStateCache[i].payload = "";
  }
}

static void mqttPublish(const char *suffix, const String &value, bool retained = true) {
  if (!mqttClient.connected()) return;
  int freeSlot = -1;
  for (uint8_t i = 0; i < MQTT_STATE_CACHE_SIZE; ++i) {
    if (mqttStateCache[i].topic == suffix) {
      if (!mqttForcePublish && mqttStateCache[i].payload == value) return;
      bool published = mqttClient.publish(mqttTopic(suffix).c_str(), value.c_str(), retained);
      if (mqttForcePublish) vTaskDelay(pdMS_TO_TICKS(20));
      if (published) {
        mqttStateCache[i].payload = value;
      }
      return;
    }
    if (freeSlot < 0 && mqttStateCache[i].topic.length() == 0) freeSlot = i;
  }
  bool published = mqttClient.publish(mqttTopic(suffix).c_str(), value.c_str(), retained);
  if (mqttForcePublish) vTaskDelay(pdMS_TO_TICKS(20));
  if (!published) return;
  if (freeSlot >= 0) {
    mqttStateCache[freeSlot].topic = suffix;
    mqttStateCache[freeSlot].payload = value;
  }
}

static void publishMqttState(bool force = false) {
  if (!mqttClient.connected()) return;
  AppConfig cfgSnapshot;
  lockCfg();
  cfgSnapshot = cfg;
  unlockCfg();
  const AppConfig &cfg = cfgSnapshot;
  mqttForcePublish = force;
  mqttPublish("availability", "online");
  mqttPublish("number/volume/state", String(cfg.volume));
  mqttPublish("number/led_brightness/state", String(cfg.ledBrightnessPct));
  mqttPublish("switch/mic_on_off/state", cfg.micEnabled ? "ON" : "OFF");
  mqttPublish("switch/conversation_mode/state", cfg.conversationMode ? "ON" : "OFF");
  mqttPublish("switch/hotword_stream/state", cfg.hotwordStreamEnabled ? "ON" : "OFF");
  mqttPublish("switch/led_active/state", cfg.ledEnabled ? "ON" : "OFF");
  mqttPublish("sensor/version/state", VBOT_CLIENT_VERSION);
  mqttPublish("sensor/application_name/state", "VBot ESP32 Client");
  mqttPublish("sensor/package_name/state", VBOT_BUILD_TARGET);
  mqttPublish("sensor/client_name/state", cfg.clientName);
  mqttPublish("sensor/mqtt_client_name/state", cfg.mqttClientName);
  mqttPublish("sensor/wifi_name/state", WiFi.isConnected() ? WiFi.SSID() : "Không kết nối");
  mqttPublish("sensor/ip_address/state", WiFi.isConnected() ? WiFi.localIP().toString() : "Không xác định");
  mqttPublish("sensor/server_connection/state", wsReady ? "Đã kết nối" : "Mất kết nối");
  mqttPublish("sensor/server_address/state", String("ws://") + cfg.serverHost + ":" + String(cfg.serverPort) + cfg.serverPath);
  mqttPublish("sensor/mdns_status/state", mdnsStarted ? "Đang bật" : "Đang tắt");
  String speakerStatus = recording ? "Đang thu âm" : (!waitingToWake ? "Đang xử lý" : "Chờ đánh thức");
  mqttPublish("sensor/speaker_status/state", speakerStatus);
  JsonDocument media;
  bool playing = !audioIdle();
  String mediaTitle;
  String mediaCover;
  String mediaAudioUrl;
  String ttsText;
  uint8_t ttsStateValue;
  uint32_t ttsUpdatedAt;
  lockMediaState();
  if (!playing) {
    currentMqttMediaTitle = "";
    currentMqttMediaCover = "";
  }
  mediaTitle = currentMqttMediaTitle;
  mediaCover = currentMqttMediaCover;
  mediaAudioUrl = currentAudioUrl;
  ttsText = currentGoogleTtsText;
  ttsStateValue = googleTtsState;
  ttsUpdatedAt = googleTtsUpdatedAtMs;
  unlockMediaState();
  media["state"] = playing ? "playing" : "idle";
  media["title"] = playing
    ? (mediaTitle.length() ? mediaTitle : "Âm thanh VBot ESP32")
    : "";
  media["media_url"] = playing ? mediaAudioUrl : "";
  media["cover"] = playing
      ? (mediaCover.length()
        ? mediaCover
        : String("http://") + cfg.serverHost + "/assets/img/logo.png")
    : "";
  media["source"] = playing ? "App VBot" : "Không phát";
  media["source_kind"] = playing ? "vbot" : "none";
  media["position"] = 0;
  media["duration"] = 0;
  media["volume"] = cfg.volume;
  String mediaPayload;
  serializeJson(media, mediaPayload);
  mqttPublish("media_player/state", mediaPayload);
  JsonDocument tts;
  const char *ttsState = ttsStateValue == 1 ? "queued"
    : ttsStateValue == 2 ? "playing"
    : ttsStateValue == 3 ? "error"
    : "idle";
  tts["state"] = ttsState;
  tts["text"] = ttsText;
  tts["source"] = "google_translate";
  tts["error"] = ttsStateValue == 3 ? "Không phát được Google TTS qua audio_proxy" : "";
  tts["updated_at"] = ttsUpdatedAt;
  String ttsPayload;
  serializeJson(tts, ttsPayload);
  mqttPublish("tts/state", ttsPayload);
  mqttForcePublish = false;
}

static void mqttSetVolume(int value) {
  lockCfg();
  cfg.volume = constrain(value, 0, 100);
  uint8_t volume = cfg.volume;
  unlockCfg();
  volumeSaveValue = volume;
  volumeSaveAtMs = millis() + VOLUME_SAVE_DELAY_MS;
  volumeSavePending = true;
  volumeSaveFeedbackPending = false;
  mqttStateDirty = true;
  AudioCommand cmd = {};
  cmd.type = AUDIO_CMD_SET_VOLUME;
  queueAudioCommand(cmd);
  showVolumeOverlay(volume);
}

static bool parseMqttNumber(const String &payload, float &value) {
  if (!payload.length()) return false;
  char *end = nullptr;
  value = strtof(payload.c_str(), &end);
  return end && end != payload.c_str() && *end == '\0' && isfinite(value);
}

static bool parseMqttSwitch(const String &payload, bool &enabled) {
  if (payload.equalsIgnoreCase("ON") || payload == "1" || payload.equalsIgnoreCase("true")) {
    enabled = true;
    return true;
  }
  if (payload.equalsIgnoreCase("OFF") || payload == "0" || payload.equalsIgnoreCase("false")) {
    enabled = false;
    return true;
  }
  return false;
}

static void logInvalidMqttPayload(const String &topic, const String &payload) {
  LOGF("Bỏ qua MQTT payload không hợp lệ: topic=%s payload=%s", topic.c_str(), payload.c_str());
}

static void mqttMessageCallback(char *topicRaw, uint8_t *payloadRaw, unsigned int length) {
  String topicValue(topicRaw);
  String payload;
  payload.reserve(length);
  for (unsigned int i = 0; i < length; ++i) payload += (char)payloadRaw[i];
  payload.trim();
  bool forceSync = topicValue.endsWith("/script/state_sync/set");
  if (topicValue.endsWith("/number/volume/set")) {
    float value;
    if (!parseMqttNumber(payload, value)) {
      logInvalidMqttPayload(topicValue, payload);
      return;
    }
    mqttSetVolume((int)round(value));
  } else if (topicValue.endsWith("/number/led_brightness/set")) {
    float value;
    if (!parseMqttNumber(payload, value)) {
      logInvalidMqttPayload(topicValue, payload);
      return;
    }
    lockCfg();
    cfg.ledBrightnessPct = constrain((int)round(value), 0, 100);
    uint8_t brightness = cfg.ledBrightnessPct;
    unlockCfg();
    ledRefreshRequested = true;
    ledBrightnessSaveValue = brightness;
    ledBrightnessSaveAtMs = millis() + VOLUME_SAVE_DELAY_MS;
    ledBrightnessSavePending = true;
    mqttStateDirty = true;
  } else if (topicValue.endsWith("/switch/mic_on_off/set")) {
    bool enabled;
    if (!parseMqttSwitch(payload, enabled)) {
      logInvalidMqttPayload(topicValue, payload);
      return;
    }
    if (!hardwareGpioValid) {
      LOG("Bỏ qua lệnh MQTT Mic vì GPIO đang ở chế độ an toàn.");
      return;
    }
    lockCfg();
    bool micEnabled = cfg.micEnabled;
    unlockCfg();
    if (enabled != micEnabled) micShortPress();
  } else if (topicValue.endsWith("/switch/conversation_mode/set")) {
    bool enabled;
    if (!parseMqttSwitch(payload, enabled)) {
      logInvalidMqttPayload(topicValue, payload);
      return;
    }
    lockCfg();
    cfg.conversationMode = enabled;
    bool conversationMode = cfg.conversationMode;
    unlockCfg();
    lockNvs();
    prefs.putBool("conv", conversationMode);
    unlockNvs();
    clientConfigSyncRequested = true;
  } else if (topicValue.endsWith("/switch/hotword_stream/set")) {
    bool enabled;
    if (!parseMqttSwitch(payload, enabled)) {
      logInvalidMqttPayload(topicValue, payload);
      return;
    }
    lockCfg();
    cfg.hotwordStreamEnabled = enabled;
    bool hotwordStreamEnabled = cfg.hotwordStreamEnabled;
    unlockCfg();
    lockNvs();
    prefs.putBool("hword", hotwordStreamEnabled);
    unlockNvs();
  } else if (topicValue.endsWith("/switch/led_active/set")) {
    bool enabled;
    if (!parseMqttSwitch(payload, enabled)) {
      logInvalidMqttPayload(topicValue, payload);
      return;
    }
    if (enabled && !hardwareGpioValid) {
      LOG("Bỏ qua lệnh MQTT bật LED vì GPIO đang ở chế độ an toàn.");
      return;
    }
    if (enabled != cfg.ledEnabled) {
      lockCfg();
      cfg.ledEnabled = enabled;
      unlockCfg();
      lockNvs();
      prefs.putBool("leden", cfg.ledEnabled);
      unlockNvs();
      // FastLED chỉ được thao tác bởi ledTask/setup. Restart áp dụng thay đổi
      // controller mà không gọi FastLED đồng thời từ callback MQTT.
      ledRefreshRequested = true;
      if (!enabled) setLedEffect(LED_OFF);
      scheduleRestart(500, "MQTT thay đổi trạng thái LED");
    }
  } else if (topicValue.endsWith("/script/media_control/set")) {
    if (payload.equalsIgnoreCase("STOP")) {
      if (!stopAudioPlayback(true)) LOG("Không thể đưa lệnh MQTT dừng media vào hàng đợi audio.");
    }
    else {
      if (!hardwareGpioValid) {
        LOG("Bỏ qua lệnh MQTT phát media vì GPIO đang ở chế độ an toàn.");
        return;
      }
      JsonDocument command;
      DeserializationError error = deserializeJson(command, payload);
      if (!error) {
        const char *url = command["media_link"] | "";
        if (validAudioUrl(url)) {
          lockMediaState();
          currentMqttMediaTitle = String(command["media_name"] | "");
          currentMqttMediaCover = String(command["media_cover"] | "");
          unlockMediaState();
          if (!playMp3Url(url)) LOG("Không thể đưa lệnh MQTT media vào hàng đợi audio.");
        } else logInvalidMqttPayload(topicValue, payload);
      } else {
        logInvalidMqttPayload(topicValue, payload);
      }
    }
  } else if (topicValue.endsWith("/script/vbot_tts/set")) {
    if (!hardwareGpioValid) {
      LOG("Bỏ qua lệnh MQTT TTS vì GPIO đang ở chế độ an toàn.");
      return;
    }
    if (!queueGoogleTranslateTts(payload)) {
      lockMediaState();
      currentGoogleTtsText = payload.substring(0, GOOGLE_TTS_MAX_TEXT_BYTES);
      googleTtsState = 3;
      googleTtsUpdatedAtMs = millis();
      unlockMediaState();
      mqttStateDirty = true;
    }
  } else if (topicValue.endsWith("/script/volume_control/set")) {
    if (payload.equalsIgnoreCase("UP")) volUpShortPress();
    else if (payload.equalsIgnoreCase("DOWN")) volDownShortPress();
    else {
      logInvalidMqttPayload(topicValue, payload);
      return;
    }
  } else if (topicValue.endsWith("/script/button_control/set")) {
    if (!hardwareGpioValid) {
      LOG("Bỏ qua lệnh MQTT nút nhấn vì GPIO đang ở chế độ an toàn.");
      return;
    }
    if (payload.equalsIgnoreCase("WAKEUP")) wakeShortPress();
    else {
      logInvalidMqttPayload(topicValue, payload);
      return;
    }
  } else if (topicValue.endsWith("/script/power_action/set")) {
    if (payload.equalsIgnoreCase("RESTART_INTERFACE") || payload.equalsIgnoreCase("RESTART_VBOT_SERVICE")) {
      scheduleRestart(300, "Lệnh power_action từ MQTT");
    } else {
      logInvalidMqttPayload(topicValue, payload);
      return;
    }
  }
  if (forceSync) {
    // Publish hàng loạt bên ngoài callback để không tranh chấp WiFi với
    // WebSocket đang truyền dữ liệu microphone.
    mqttForceSyncRequested = true;
  } else {
    mqttStateDirty = true;
  }
}

static uint32_t mqttRuntimeStateHash() {
  AppConfig cfgSnapshot;
  lockCfg();
  cfgSnapshot = cfg;
  unlockCfg();
  const AppConfig &cfg = cfgSnapshot;
  uint32_t hash = 2166136261UL;
  auto mix = [&hash](uint32_t value) {
    hash ^= value;
    hash *= 16777619UL;
  };
  mix(cfg.volume);
  mix(cfg.ledBrightnessPct);
  mix(cfg.micEnabled);
  mix(cfg.conversationMode);
  mix(cfg.hotwordStreamEnabled);
  mix(cfg.ledEnabled);
  mix(WiFi.isConnected());
  mix(WiFi.isConnected() ? (uint32_t)WiFi.localIP() : 0);
  mix(wsReady);
  mix(recording);
  mix(waitingToWake);
  mix(mdnsStarted);
  mix(!audioIdle());
  return hash;
}

static void setupMdns() {
  if (mdnsStarted || !cfg.mdnsEnabled || !WiFi.isConnected()) return;
  String hostname = String(cfg.mqttClientName);
  hostname.toLowerCase();
  for (size_t i = 0; i < hostname.length(); ++i) {
    char c = hostname[i];
    if (!isalnum((unsigned char)c) && c != '-') hostname.setCharAt(i, '-');
  }
  if (!MDNS.begin(hostname.c_str())) return;
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("vbot-assistant", "tcp", 80);
  MDNS.addServiceTxt(String("vbot-assistant"), String("tcp"), String("device_id"), String(cfg.mqttClientName));
  MDNS.addServiceTxt("vbot-assistant", "tcp", "url_api", WiFi.localIP().toString());
  String mdnsDeviceName = String(VBOT_BUILD_TARGET) + " VBot Client";
  MDNS.addServiceTxt("vbot-assistant", "tcp", "name", mdnsDeviceName);
  MDNS.addServiceTxt("vbot-assistant", "tcp", "version", VBOT_CLIENT_VERSION);
  MDNS.addServiceTxt("vbot-assistant", "tcp", "device_type", "esp32_client");
  MDNS.addServiceTxt("vbot-assistant", "tcp", "board", VBOT_BUILD_TARGET);
  MDNS.addServiceTxt("vbot-assistant", "tcp", "build_target", VBOT_BUILD_TARGET);
  mdnsStarted = true;
}

static void mqttSubscribePaced(const char *suffix) {
  mqttClient.subscribe(mqttTopic(suffix).c_str(), 1);
  vTaskDelay(pdMS_TO_TICKS(20));
}

static void maintainMdns() {
  if (!cfg.mdnsEnabled || !WiFi.isConnected()) {
    if (mdnsStarted) {
      MDNS.end();
      mdnsStarted = false;
    }
    return;
  }
  if (!mdnsStarted) setupMdns();
}

static void maintainMqtt() {
  // OTA uses the same WiFi radio and AsyncTCP buffers. Keep the MQTT socket
  // allocated but pause broker traffic until the firmware upload has ended.
  if (otaUpdateActive || !cfg.mqttEnabled || !WiFi.isConnected() || !cfg.mqttHost[0]) return;
  if (!mqttClient.connected()) {
    uint32_t now = millis();
    if (now - mqttLastReconnectMs < 5000) return;
    mqttLastReconnectMs = now;
    mqttNetworkBusy = true;
    if (micQueue) xQueueReset(micQueue);
    vTaskDelay(pdMS_TO_TICKS(50));
    String willTopic = mqttTopic("availability");
    bool connected = cfg.mqttUsername[0]
      ? mqttClient.connect(cfg.mqttClientName, cfg.mqttUsername, cfg.mqttPassword, willTopic.c_str(), 1, true, "offline")
      : mqttClient.connect(cfg.mqttClientName, willTopic.c_str(), 1, true, "offline");
    if (!connected) {
      mqttNetworkBusy = false;
      return;
    }
    LOGF("MQTT đã kết nối broker %s:%u, bắt đầu đồng bộ trạng thái.", cfg.mqttHost, cfg.mqttPort);
    mqttSubscribePaced("number/volume/set");
    mqttSubscribePaced("number/led_brightness/set");
    mqttSubscribePaced("switch/+/set");
    mqttSubscribePaced("script/media_control/set");
    mqttSubscribePaced("script/vbot_tts/set");
    mqttSubscribePaced("script/volume_control/set");
    mqttSubscribePaced("script/button_control/set");
    mqttSubscribePaced("script/power_action/set");
    mqttSubscribePaced("script/state_sync/set");
    clearMqttStateCache();
    publishMqttState(true);
    LOG("MQTT đã đồng bộ trạng thái ban đầu.");
    mqttStateDirty = false;
    mqttLastStateHash = mqttRuntimeStateHash();
    mqttLastPublishMs = millis();
    if (micQueue) xQueueReset(micQueue);
    mqttNetworkBusy = false;
  }
  mqttClient.loop();
  uint32_t now = millis();
  if (mqttForceSyncRequested) {
    mqttForceSyncRequested = false;
    mqttNetworkBusy = true;
    if (micQueue) xQueueReset(micQueue);
    vTaskDelay(pdMS_TO_TICKS(50));
    clearMqttStateCache();
    publishMqttState(true);
    mqttStateDirty = false;
    mqttLastStateHash = mqttRuntimeStateHash();
    mqttLastPublishMs = millis();
    if (micQueue) xQueueReset(micQueue);
    mqttNetworkBusy = false;
    LOG("MQTT đã đồng bộ trạng thái theo yêu cầu thủ công.");
    now = millis();
  }
  if (now - mqttLastStateCheckMs >= 500) {
    mqttLastStateCheckMs = now;
    uint32_t stateHash = mqttRuntimeStateHash();
    if (stateHash != mqttLastStateHash) {
      mqttLastStateHash = stateHash;
      mqttStateDirty = true;
    }
  }
  if (mqttStateDirty || now - mqttLastPublishMs >= 60000) {
    mqttLastPublishMs = now;
    publishMqttState();
    mqttStateDirty = false;
  }
}

void setup() {
  Serial.begin(115200);
  if (ESP.getChipCores() < 2) {
    Serial.printf("VBot Client ESP32 yêu cầu tối thiểu 2 lõi CPU. Đã phát hiện: %u core(s).\n", ESP.getChipCores());
    fatalHalt("Đã dừng khởi động boot vì CPU không đủ 2 core để chạy chương trình");
  }
  cfgMutex = xSemaphoreCreateMutex();
  if (!cfgMutex) fatalHalt("Không tạo được cấu hình mutex Semaphore");
  mediaStateMutex = xSemaphoreCreateMutex();
  nvsMutex = xSemaphoreCreateMutex();
  if (!nvsMutex) fatalHalt("Khong tao duoc NVS mutex Semaphore");
  micI2sMutex = xSemaphoreCreateMutex();
  if (!micI2sMutex) fatalHalt("Khong tao duoc microphone I2S mutex Semaphore");
  updateMutex = xSemaphoreCreateMutex();
  if (!updateMutex) fatalHalt("Khong tao duoc update mutex Semaphore");
  if (!mediaStateMutex) fatalHalt("Không tạo được media state mutex Semaphore");
  loadConfig();
  migrateUnsafeDefaultPins();
  hardwareGpioErrors = validateHardwareGpioPins(
    cfg.ledEnabled ? cfg.pinLed : -1,
    cfg.buttonsEnabled && !inputModeRotary() ? cfg.pinBtnMic : -1,
    cfg.buttonsEnabled && !inputModeRotary() ? cfg.pinBtnVolUp : -1,
    cfg.buttonsEnabled && !inputModeRotary() ? cfg.pinBtnVolDown : -1,
    cfg.buttonsEnabled && !inputModeRotary() ? cfg.pinBtnWake : -1,
    cfg.buttonsEnabled && inputModeRotary() ? cfg.pinRotaryClk : -1,
    cfg.buttonsEnabled && inputModeRotary() ? cfg.pinRotaryDt : -1,
    cfg.buttonsEnabled && inputModeRotary() ? cfg.pinRotarySw : -1,
    vbotServerStartupEnabled() ? cfg.micBclk : -1,
    vbotServerStartupEnabled() ? cfg.micWs : -1,
    vbotServerStartupEnabled() ? cfg.micDin : -1,
    cfg.dacBclk, cfg.dacLrc, cfg.dacDout
  );
  hardwareGpioValid = hardwareGpioErrors.length() == 0;
  if (!hardwareGpioValid) {
    Serial.println("GPIO configuration is invalid. Starting WebUI safe mode; hardware GPIO initialization is disabled:");
    Serial.println(hardwareGpioErrors);
  }
  LOG("Booting - Khởi động chương trình VBot Client ESP32");

  if (hardwareGpioValid) initLed();

  WiFi.mode(WIFI_STA);
  // Truyền mic và audio thời gian thực cần độ trễ ổn định hơn chế độ tiết kiệm điện WiFi.
  WiFi.setSleep(false);
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  String apName = configPortalSsid();
  if (!wm.autoConnect(apName.c_str())) {
    LOG("Lỗi hết thời gian chờ kết nối WiFi, tiếp tục thực hiện tác vụ thử lại kết nối.");
  }

  micQueue = xQueueCreate(WS_SEND_QUEUE_LEN, sizeof(MicFrame));
  audioQueue = xQueueCreate(DAC_QUEUE_LEN, sizeof(AudioCommand));
  pcmFreeQueue = xQueueCreate(DAC_QUEUE_LEN, sizeof(uint8_t));
  if (!micQueue) fatalHalt("Không tạo được micQueue.");
  if (!audioQueue) fatalHalt("Không tạo được audioQueue.");
  if (!pcmFreeQueue) fatalHalt("Không tạo được pcmFreeQueue.");
  for (uint8_t slot = 0; slot < DAC_QUEUE_LEN; ++slot) {
    xQueueSend(pcmFreeQueue, &slot, 0);
  }
  if (hardwareGpioValid) {
    initButtons();
    if (vbotServerStartupEnabled()) initMic();
    initAudioMp3();
    installSilentDac();
  }
  if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
    LOG("LittleFS mount lỗi, WebUI sẽ không hoạt động cho tới khi upload file: littlefs.bin");
  }
  if (LittleFS.totalBytes() > 0) refreshLocalSoundCache();
  updateTaskHandle = createPinnedTaskRequired(updateCheckTask, "update", UPDATE_TASK_STACK, 1, 1);
  initWeb();
  setupMdns();
  mqttClient.setServer(cfg.mqttHost, cfg.mqttPort);
  mqttClient.setCallback(mqttMessageCallback);
  mqttClient.setBufferSize(768);
  if (hardwareGpioValid && vbotServerStartupEnabled()) {
    connectSocket();
  } else {
    wsReady = false;
    recording = false;
    waitingToWake = false;
    LOG("Chế độ khởi chạy độc lập: bỏ qua Mic và kết nối WebSocket tới máy chủ VBot.");
  }
  if (hardwareGpioValid && cfg.startupSoundEnabled && localStartupAvailable) {
    setLedEffect(LED_STARTUP);
    playMp3FilePath("/sound/start_up.mp3", false, LED_STARTUP, true);
  } else {
    setLedEffect(micActive() ? LED_OFF : LED_MUTE);
  }

  // Chỉ cho hiệu ứng FastLED chạy sau khi WiFiManager, I2S, LittleFS và WebUI
  // đã khởi tạo xong. Việc chạy task LED song song với WiFiManager trên core 0
  // có thể che mất panic ban đầu và cuối cùng chỉ còn thấy TG1WDT_SYS_RST.
  if (hardwareGpioValid) ledTaskHandle = createPinnedTaskRequired(ledTask, "led", LED_TASK_STACK, 1, 0);
  if (hardwareGpioValid && vbotServerStartupEnabled()) {
    micTaskHandle = createPinnedTaskRequired(micTask, "mic", MIC_TASK_STACK, 3, 0);
    websocketTaskHandle = createPinnedTaskRequired(websocketTask, "websocket", WEBSOCKET_TASK_STACK, 4, 1);
  }
  if (hardwareGpioValid) {
    dacTaskHandle = createPinnedTaskRequired(dacTask, "dac", DAC_TASK_STACK, 3, 1);
    buttonTaskHandle = createPinnedTaskRequired(buttonTask, "button", BUTTON_TASK_STACK, 2, 0);
  }
  wifiTaskHandle = createPinnedTaskRequired(wifiTask, "wifi", WIFI_TASK_STACK, 1, 0);
}

void loop() {
  ElegantOTA.loop();
  maintainMdns();
  maintainMqtt();
  runPendingResetSequences();
  if (restartAtMs && deadlineReached(restartAtMs)) {
    if (otaUpdateActive || fsMaintenanceActive) {
      if (!restartBlockedLogged) {
        restartBlockedLogged = true;
        UART_LOGF("Restart đang bị trì hoãn vì %s đang hoạt động. Lý do restart: %s",
             otaUpdateActive ? "OTA" : "bảo trì LittleFS", restartReason);
      }
    } else {
      restartAtMs = 0;
      UART_LOGF("THỰC HIỆN KHỞI ĐỘNG LẠI. Lý do: %s; free=%u, min=%u",
           restartReason, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
      Serial.flush();
      delay(50);
      ESP.restart();
    }
  }
  vTaskDelay(pdMS_TO_TICKS(50));
}
