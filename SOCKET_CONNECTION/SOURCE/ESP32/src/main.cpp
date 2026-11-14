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
#include <esp_wifi.h>
#include "vbot_wakenet.h"
#include "vbot_dlna.h"
#include "vbot_airplay.h"
#include "vbot_bt_slave.h"

#if defined(CONFIG_FREERTOS_UNICORE) && CONFIG_FREERTOS_UNICORE
#error "VBot Client ESP32 yêu cầu chip ESP có CPU 2 core trở lên. Các chip có CPU 1 core không được hỗ trợ."
#endif

#define VBOT_I2S_COMM_FORMAT I2S_COMM_FORMAT_STAND_I2S

static const char *VBOT_CLIENT_VERSION = "14/11/2026";
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
static constexpr uint8_t WS_SEND_QUEUE_LEN = 4;
static constexpr uint8_t DAC_QUEUE_LEN = 6;
static constexpr size_t PCM_POOL_BUFFER_SIZE = 2048;
static constexpr size_t MP3_STREAM_BUFFER_SIZE = 4 * 1024;
static constexpr size_t MP3_STREAM_BUFFER_SIZE_PSRAM = 64 * 1024;
static constexpr size_t VAD_PREROLL_FRAMES = 4;
static constexpr uint32_t VAD_HANGOVER_MS = 850;
static constexpr int I2S_DMA_BUFFER_COUNT = 4;
static constexpr int I2S_DMA_BUFFER_LENGTH = 256;
#if VBOT_WAKENET_SUPPORTED
static constexpr uint32_t MIC_TASK_STACK = 10240;
#else
static constexpr uint32_t MIC_TASK_STACK = 4096;
#endif
static constexpr uint32_t WEBSOCKET_TASK_STACK = 5120;
static constexpr uint32_t DAC_TASK_STACK = 5120;
// FastLED trên ESP32-S3 cần nhiều stack hơn ESP32 thường. Stack 2048 có thể làm
// core chạy LED rơi vào panic/double exception trước khi setup() hoàn tất, sau
// đó TG1WDT reset toàn hệ thống mà chưa kịp in Guru Meditation/backtrace.
static constexpr uint32_t LED_TASK_STACK = 3072;
static constexpr uint32_t BUTTON_TASK_STACK = 4096;
static constexpr uint32_t WIFI_TASK_STACK = 5120;
static constexpr uint32_t UPDATE_TASK_STACK = 8192;
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
  char buttonActiveLevel[8] = "low";
  uint8_t ledBrightnessPct = 80;
  uint16_t ledCount = 12;
  char ledThinkColor[8] = "#0000FF";
  char ledMuteColor[8] = "#FF8C00";
  char ledErrorColor[8] = "#FF2814";
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
  bool btSlaveEnabled = false;
  int btSlaveBclk =
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    14;
#else
    14;
#endif
  int btSlaveWs =
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    21;
#else
    13;
#endif
  int btSlaveDin =
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    47;
#else
    35;
#endif
  int btSlaveTx =
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    1;
#else
    1;
#endif
  int btSlaveRx =
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    2;
#else
    3;
#endif
  bool micEnabled = true;
  bool hotwordStreamEnabled = true;
  bool wakenetEnabled = false;
  uint8_t wakenetModel = (uint8_t)WAKENET_MODEL_HILILI_TTS;
  bool micVadEnabled = true;
  bool conversationMode = true;
  bool startupSoundEnabled = true;
  bool serialLogs = true;
  bool mdnsEnabled = true;
  bool dlnaEnabled = true;    // DLNA / UPnP MediaRenderer (Mặc định bật để phát nhạc từ điện thoại/PC)
  bool airplayEnabled = true; // Apple AirPlay Receiver (Mặc định bật để hiện trong Control Center iPhone)
  bool nsEnabled = false;
  bool agcEnabled = false;
  int agcGain = 12;
  uint8_t ledSpeakEffect = 0; // 0: Mặc định, 1: VU Meter, 2: Audio Pulse, 3: Audio Fire
  uint8_t ledThinkEffect = 0; // 0: Mặc định (Sáng toàn bộ LED), 1: Spinning Comet, 2: Breathe
  uint8_t ledMuteEffect = 0;  // 0: Mặc định (Sáng toàn bộ LED), 1: Breathe, 2: Spinning Comet
  uint8_t ledErrorEffect = 0; // 0: Thở dịu nhẹ (Breathe), 1: Mặc định (Sáng toàn bộ LED), 2: Chớp tắt cảnh báo (Strobe/Blink), 3: Sao chổi xoay tròn (Spinning Comet)
  bool dfsEnabled = false;    // Dynamic Frequency Scaling (Mặc định tắt để âm thanh I2S tuyệt đối ổn định)
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
  uint16_t buttonStartupDelayMs = 3000;
  char wakeLongAction[24] = "stop_audio";
  char wakeDoubleAction[24] = "none";
  char micLongAction[24] = "none";
  char micDoubleAction[24] = "none";
  char volUpLongAction[24] = "none";
  char volUpDoubleAction[24] = "none";
  char volDownLongAction[24] = "none";
  char volDownDoubleAction[24] = "none";
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

enum ManualLedEffect : uint8_t {
  MANUAL_LED_SOLID,
  MANUAL_LED_BREATHE,
  MANUAL_LED_COLOR_LOOP,
  MANUAL_LED_PULSE,
  MANUAL_LED_RAINBOW,
  MANUAL_LED_COLOR_WIPE,
  MANUAL_LED_CHASE,
  MANUAL_LED_THEATER_CHASE,
  MANUAL_LED_TWINKLE,
  MANUAL_LED_SPARKLE,
  MANUAL_LED_SCANNER,
  MANUAL_LED_FIRE,
  MANUAL_LED_CANDLE,
  MANUAL_LED_STROBE
};

struct ManualLedState {
  bool on = false;
  uint8_t red = 255;
  uint8_t green = 180;
  uint8_t blue = 100;
  uint8_t brightness = 255;
  ManualLedEffect effect = MANUAL_LED_SOLID;
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
  uint8_t clickCount = 0;
  uint32_t lastRawChangeMs = 0;
  uint32_t pressStartMs = 0;
  uint32_t lastRepeatMs = 0;
  uint32_t releaseMs = 0;
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
static volatile uint32_t webUiLastActiveMs = 0;
static std::atomic<bool> webUiStateInFlight{false};

static inline void markWebUiActive() {
  webUiLastActiveMs = millis();
}

static inline bool isWebUiActive() {
  return webUiStateInFlight.load() ||
         (webUiLastActiveMs != 0 && (uint32_t)(millis() - webUiLastActiveMs) < 6000);
}
static constexpr uint8_t MQTT_STATE_CACHE_SIZE = 26;
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
    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (pendingMicOffset > 0) {
        // Frame đã gửi một phần ra socket: chờ tối đa 10ms để giải phóng lwIP và hoàn tất frame
        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(socketFd, &writeSet);
        timeval timeout = {0, 10000};
        int ready = select(socketFd + 1, nullptr, &writeSet, nullptr, &timeout);
        if (ready > 0 && FD_ISSET(socketFd, &writeSet)) {
          sent = ::send(socketFd,
                        pendingMicFrame + pendingMicOffset,
                        pendingMicLength - pendingMicOffset,
                        MSG_DONTWAIT);
          if (sent > 0) {
            pendingMicOffset += (size_t)sent;
            if (pendingMicOffset >= pendingMicLength) {
              resetPendingMicFrame();
              return 1;
            }
          }
        }
      }
      return 0;
    }
    return -1;
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

  void flushPendingMicFrameFully() {
    if (!hasPendingMicFrame()) return;
    if (!pendingMicFrameStarted()) {
      resetPendingMicFrame();
      return;
    }
    uint32_t startMs = millis();
    while (hasPendingMicFrame() && (uint32_t)(millis() - startMs) < 200) {
      if (flushPendingMicFrame() != 0) break;
      delay(1);
    }
    if (hasPendingMicFrame()) {
      disconnect();
      resetPendingMicFrame();
    }
  }

  bool sendTXT(const char *payload, size_t length = 0) {
    flushPendingMicFrameFully();
    return WebSocketsClient::sendTXT(payload, length);
  }
  bool sendTXT(String &payload) {
    flushPendingMicFrameFully();
    return WebSocketsClient::sendTXT(payload);
  }
  bool sendTXT(const String &payload) {
    flushPendingMicFrameFully();
    return WebSocketsClient::sendTXT(payload.c_str(), payload.length());
  }
  bool sendTXT(uint8_t *payload, size_t length = 0, bool headerToPayload = false) {
    flushPendingMicFrameFully();
    return WebSocketsClient::sendTXT(payload, length, headerToPayload);
  }
};
VBotWebSocketsClient ws;
AudioGeneratorMP3 *mp3 = nullptr;
AudioFileSource *mp3File = nullptr;
AudioFileSource *mp3RawFile = nullptr;
static volatile uint16_t currentAudioPeak = 0;

class AudioOutputVBotAEC : public AudioOutputI2S {
private:
  int16_t aecChunk[128];
  size_t aecChunkCount = 0;
public:
  AudioOutputVBotAEC(int port = 0, int output_mode = EXTERNAL_I2S, int dma_buf_count = 8, int use_apll = APLL_DISABLE)
    : AudioOutputI2S(port, output_mode, dma_buf_count, use_apll) {}
  
  virtual bool ConsumeSample(int16_t sample[2]) override {
    int32_t s0 = abs((int32_t)sample[0]);
    int32_t s1 = abs((int32_t)sample[1]);
    uint16_t p = (uint16_t)(s0 > s1 ? s0 : s1);
    if (p > currentAudioPeak) {
      currentAudioPeak = p;
    }
    if (vbot_aec_is_active()) {
      int16_t mono = (int16_t)(((int32_t)sample[0] + (int32_t)sample[1]) / 2);
      aecChunk[aecChunkCount++] = mono;
      if (aecChunkCount >= 128) {
        vbot_aec_feed_ref(aecChunk, 128);
        aecChunkCount = 0;
      }
    }
    return AudioOutputI2S::ConsumeSample(sample);
  }

  virtual void flush() override {
    if (aecChunkCount > 0 && vbot_aec_is_active()) {
      vbot_aec_feed_ref(aecChunk, aecChunkCount);
      aecChunkCount = 0;
    }
    AudioOutputI2S::flush();
  }

  virtual bool stop() override {
    aecChunkCount = 0;
    return AudioOutputI2S::stop();
  }
};
AudioOutputI2S *mp3Out = nullptr;
static uint8_t *mp3PsramStreamBuffer = nullptr;
static uint8_t *mp3PreallocBuffer = nullptr;
static constexpr size_t MP3_PREALLOC_SIZE = AudioGeneratorMP3::preAllocSize();
CRGB *leds = nullptr;
String activeSessionId;
struct RestoreUploadContext {
  size_t length;
  bool tooLarge;
  char data[8193];
};
struct LedApiBody {
  size_t length = 0;
  bool tooLarge = false;
  char data[513] = {0};
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
static uint8_t (*pcmPool)[PCM_POOL_BUFFER_SIZE] = nullptr;
static MicFrame *vadPreRoll = nullptr;
static size_t vadPreRollWriteIdx = 0;
static size_t vadPreRollCount = 0;
static float vadNoiseFloorEma = 120.0f;
static uint32_t vadHoldUntilMs = 0;
volatile bool micVadGateOpen = false;
volatile int32_t micVadNoiseFloor = 120;
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
ManualLedState manualLedState;
portMUX_TYPE manualLedMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool ledRefreshRequested = false;
volatile bool hardwareGpioValid = true;
String hardwareGpioErrors;
volatile bool fsMaintenanceActive = false;
volatile bool wsReady = false;
volatile uint32_t wsForceReconnectAtMs = 0;
uint32_t wsLastReconnectAttemptMs = 0;
volatile bool recording = false;
volatile uint32_t recordingDeadlineMs = 0;
volatile uint32_t recordingDurationMs = 5000U;
volatile bool waitingToWake = true;
volatile bool pendingAutoContinue = false;
volatile bool clientConfigSyncRequested = false;
volatile bool temporaryMicEnabled = false;
volatile bool pcmTxInstalled = false;
static uint32_t currentPcmSampleRate = 16000;
volatile bool micRxInstalled = false;
volatile bool pcmPlaybackActive = false;
volatile bool audioOpening = false;
volatile bool startupAudioPlaying = false;
volatile bool micSuspendedForAudio = false;
volatile uint32_t lastPcmPlaybackMs = 0;
static constexpr uint32_t PCM_LED_HOLD_MS = 1200;
volatile uint32_t testLedUntilMs = 0;
volatile bool wakeSkipRequest = false;
volatile bool stopRecordingRequested = false;
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
volatile uint8_t preMuteVolume = 50;
volatile bool volumeSaveFeedbackPending = false;
volatile bool volumeFeedbackPending = false;
volatile uint32_t volumeFeedbackAtMs = 0;
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
    if (!psramFound() && (ESP.getFreeHeap() < 55000U || ESP.getMaxAllocHeap() < 36000U)) {
      cb.st(STATUS_HTTPFAIL, PSTR("Không đủ heap liên tục cho luồng HTTPS"));
      return false;
    }
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

static void sendCorsJsonBody(AsyncWebServerRequest *request, int status, const String &body) {
  AsyncWebServerResponse *response = request->beginResponse(status, CONTENT_JSON, body);
  response->addHeader("Access-Control-Allow-Origin", "*");
  response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  response->addHeader("Access-Control-Allow-Headers", "Content-Type");
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
  LightFileResponse(File &&source, const char *contentType, bool gzip, bool cache) : file(std::move(source)) {
    _code = 200;
    _contentType = contentType;
    _contentLength = file.size();
    if (gzip) addHeader("Content-Encoding", "gzip");
    addHeader("Cache-Control", cache ? "private, no-cache" : "no-store");
  }
  bool _sourceValid() const override { return (bool)file; }
  size_t _fillBuffer(uint8_t *buffer, size_t maxLen) override { return file.read(buffer, maxLen); }
};

static void sendLittleFsFile(AsyncWebServerRequest *request, const char *path, const char *contentType, bool cache = true) {
  markWebUiActive();
  String gzPath = String(path) + ".gz";
  bool gzExists = LittleFS.exists(gzPath);
  if (clientAcceptsGzip(request) && gzExists) {
    File file = LittleFS.open(gzPath, "r");
    auto *response = new (std::nothrow) LightFileResponse(std::move(file), contentType, true, cache);
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
    auto *response = new (std::nothrow) LightFileResponse(std::move(file), contentType, false, cache);
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
  static char dir[48];
  const char *target = buildTarget();
  if (strcmp(updateFamily(), "ESP32S3") != 0) {
    if (strstr(target, "WakeNet") != nullptr) {
      strcpy(dir, "ESP32_WakeNet");
    } else {
      strcpy(dir, "ESP32");
    }
    return dir;
  }

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
  int micBclk, int micWs, int micDin, int dacBclk, int dacLrc, int dacDout,
  bool buttonActiveHigh = false
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
  copyPrefString("btnlevel", cfg.buttonActiveLevel, sizeof(cfg.buttonActiveLevel), cfg.buttonActiveLevel);
  cfg.ledBrightnessPct = prefs.getUChar("ledbr", cfg.ledBrightnessPct);
  cfg.ledCount = prefs.getUShort("ledcnt", cfg.ledCount);
  copyPrefString("lthink", cfg.ledThinkColor, sizeof(cfg.ledThinkColor), cfg.ledThinkColor);
  copyPrefString("lmute", cfg.ledMuteColor, sizeof(cfg.ledMuteColor), cfg.ledMuteColor);
  copyPrefString("lerrclr", cfg.ledErrorColor, sizeof(cfg.ledErrorColor), cfg.ledErrorColor);
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
  cfg.wakenetEnabled = prefs.getBool("wn_en", cfg.wakenetEnabled);
  cfg.wakenetModel = prefs.getUChar("wn_model", cfg.wakenetModel);
  cfg.micVadEnabled = prefs.getBool("mvad", cfg.micVadEnabled);
  cfg.conversationMode = prefs.getBool("conv", cfg.conversationMode);
  cfg.startupSoundEnabled = prefs.getBool("bootsnd", cfg.startupSoundEnabled);
  cfg.serialLogs = prefs.getBool("logs", cfg.serialLogs);
  cfg.mdnsEnabled = prefs.getBool("mdns", cfg.mdnsEnabled);
  cfg.dlnaEnabled = prefs.getBool("dlnaen", cfg.dlnaEnabled);
  cfg.airplayEnabled = prefs.getBool("airplayen", cfg.airplayEnabled);
  cfg.btSlaveEnabled = prefs.getBool("btsl_en", cfg.btSlaveEnabled);
  cfg.btSlaveBclk = prefs.getInt("btsl_bclk", cfg.btSlaveBclk);
  cfg.btSlaveWs = prefs.getInt("btsl_ws", cfg.btSlaveWs);
  cfg.btSlaveDin = prefs.getInt("btsl_din", cfg.btSlaveDin);
  cfg.btSlaveTx = prefs.getInt("btsl_tx", cfg.btSlaveTx);
  cfg.btSlaveRx = prefs.getInt("btsl_rx", cfg.btSlaveRx);
  cfg.nsEnabled = prefs.getBool("nsen", cfg.nsEnabled);
  cfg.agcEnabled = prefs.getBool("agcen", cfg.agcEnabled);
  cfg.agcGain = prefs.getInt("agcgain", cfg.agcGain);
  cfg.ledSpeakEffect = prefs.getUChar("lspef", cfg.ledSpeakEffect);
  cfg.ledThinkEffect = prefs.getUChar("lthkef", cfg.ledThinkEffect);
  cfg.ledMuteEffect = prefs.getUChar("lmuteef", cfg.ledMuteEffect);
  cfg.ledErrorEffect = prefs.getUChar("lerref", cfg.ledErrorEffect);
  cfg.dfsEnabled = prefs.getBool("dfsen", cfg.dfsEnabled);
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
  copyPrefString("wda", cfg.wakeDoubleAction, sizeof(cfg.wakeDoubleAction), cfg.wakeDoubleAction);
  copyPrefString("mla", cfg.micLongAction, sizeof(cfg.micLongAction), cfg.micLongAction);
  copyPrefString("mda", cfg.micDoubleAction, sizeof(cfg.micDoubleAction), cfg.micDoubleAction);
  copyPrefString("vupla", cfg.volUpLongAction, sizeof(cfg.volUpLongAction), cfg.volUpLongAction);
  copyPrefString("vupda", cfg.volUpDoubleAction, sizeof(cfg.volUpDoubleAction), cfg.volUpDoubleAction);
  copyPrefString("vdnla", cfg.volDownLongAction, sizeof(cfg.volDownLongAction), cfg.volDownLongAction);
  copyPrefString("vdnda", cfg.volDownDoubleAction, sizeof(cfg.volDownDoubleAction), cfg.volDownDoubleAction);
  cfg.micGain = prefs.isKey("micgain") ? prefs.getFloat("micgain", cfg.micGain) : cfg.micGain;
  cfg.pcmVolumeBoost = prefs.isKey("pcmboost") ? prefs.getFloat("pcmboost", cfg.pcmVolumeBoost) : cfg.pcmVolumeBoost;
  cfg.buttonPressDebounceMs = prefs.getUShort("bdbp", cfg.buttonPressDebounceMs);
  cfg.buttonReleaseDebounceMs = prefs.getUShort("bdbr", cfg.buttonReleaseDebounceMs);
  cfg.buttonHoldMs = prefs.getUShort("bhold", cfg.buttonHoldMs);
  cfg.buttonStartupDelayMs = prefs.getUShort("bsdelay", cfg.buttonStartupDelayMs);
  cfg.volume = constrain(cfg.volume, 0, 100);
  cfg.ledBrightnessPct = constrain(cfg.ledBrightnessPct, 0, 100);
  cfg.ledCount = constrain(cfg.ledCount, (uint16_t)1, (uint16_t)300);
  cfg.micGain = constrain(cfg.micGain, 0.1f, 16.0f);
  cfg.pcmVolumeBoost = constrain(cfg.pcmVolumeBoost, 0.1f, 16.0f);
  cfg.buttonPressDebounceMs = constrain(cfg.buttonPressDebounceMs, (uint16_t)10, (uint16_t)1000);
  cfg.buttonReleaseDebounceMs = constrain(cfg.buttonReleaseDebounceMs, (uint16_t)10, (uint16_t)1000);
  cfg.buttonHoldMs = constrain(cfg.buttonHoldMs, (uint16_t)300, (uint16_t)10000);
  cfg.buttonStartupDelayMs = constrain(cfg.buttonStartupDelayMs, (uint16_t)0, (uint16_t)10000);
  cfg.rotaryVolumeStep = constrain(cfg.rotaryVolumeStep, (uint8_t)1, (uint8_t)20);
  if (strcmp(cfg.inputMode, "rotary") != 0) strlcpy(cfg.inputMode, "button", sizeof(cfg.inputMode));
  if (strcmp(cfg.buttonActiveLevel, "high") != 0) strlcpy(cfg.buttonActiveLevel, "low", sizeof(cfg.buttonActiveLevel));
  if (strcmp(cfg.startupMode, "standalone") != 0) strlcpy(cfg.startupMode, "vbot_server", sizeof(cfg.startupMode));
  if (cfg.micChannel[0] != 'R' && cfg.micChannel[0] != 'r') strlcpy(cfg.micChannel, "L", sizeof(cfg.micChannel));
  else strlcpy(cfg.micChannel, "R", sizeof(cfg.micChannel));
  const char *wakeActions[] = {"none", "restart", "clean_all", "reset_wifi", "reset_nvs", "stop_audio", "toggle_mic", "bt_play_pause", "bt_next", "bt_prev"};
  auto validateBtnAction = [&](char *target, const char *defaultVal) {
    bool valid = false;
    for (const char *action : wakeActions) {
      if (!strcmp(target, action)) valid = true;
    }
    if (!valid) strlcpy(target, defaultVal, 24);
  };
  validateBtnAction(cfg.wakeLongAction, "stop_audio");
  validateBtnAction(cfg.wakeDoubleAction, "none");
  validateBtnAction(cfg.micLongAction, "none");
  validateBtnAction(cfg.micDoubleAction, "none");
  validateBtnAction(cfg.volUpLongAction, "none");
  validateBtnAction(cfg.volUpDoubleAction, "none");
  validateBtnAction(cfg.volDownLongAction, "none");
  validateBtnAction(cfg.volDownDoubleAction, "none");
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
  prefs.putString("btnlevel", cfg.buttonActiveLevel);
  prefs.putUChar("ledbr", cfg.ledBrightnessPct);
  prefs.putUShort("ledcnt", cfg.ledCount);
  prefs.putString("lthink", cfg.ledThinkColor);
  prefs.putString("lmute", cfg.ledMuteColor);
  prefs.putString("lerrclr", cfg.ledErrorColor);
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
  prefs.putBool("wn_en", cfg.wakenetEnabled);
  prefs.putUChar("wn_model", cfg.wakenetModel);
  prefs.putBool("mvad", cfg.micVadEnabled);
  prefs.putBool("conv", cfg.conversationMode);
  prefs.putBool("bootsnd", cfg.startupSoundEnabled);
  prefs.putBool("logs", cfg.serialLogs);
  prefs.putBool("mdns", cfg.mdnsEnabled);
  prefs.putBool("dlnaen", cfg.dlnaEnabled);
  prefs.putBool("airplayen", cfg.airplayEnabled);
  prefs.putBool("btsl_en", cfg.btSlaveEnabled);
  prefs.putInt("btsl_bclk", cfg.btSlaveBclk);
  prefs.putInt("btsl_ws", cfg.btSlaveWs);
  prefs.putInt("btsl_din", cfg.btSlaveDin);
  prefs.putInt("btsl_tx", cfg.btSlaveTx);
  prefs.putInt("btsl_rx", cfg.btSlaveRx);
  prefs.putBool("nsen", cfg.nsEnabled);
  prefs.putBool("agcen", cfg.agcEnabled);
  prefs.putInt("agcgain", cfg.agcGain);
  prefs.putUChar("lspef", cfg.ledSpeakEffect);
  prefs.putUChar("lthkef", cfg.ledThinkEffect);
  prefs.putUChar("lmuteef", cfg.ledMuteEffect);
  prefs.putUChar("lerref", cfg.ledErrorEffect);
  prefs.putBool("dfsen", cfg.dfsEnabled);
  prefs.putBool("mqen", cfg.mqttEnabled);
  prefs.putString("mqhost", cfg.mqttHost);
  prefs.putUShort("mqport", cfg.mqttPort);
  prefs.putString("mquser", cfg.mqttUsername);
  prefs.putString("mqpass", cfg.mqttPassword);
  prefs.putString("mqclient", cfg.mqttClientName);
  prefs.putString("wla", cfg.wakeLongAction);
  prefs.putString("wda", cfg.wakeDoubleAction);
  prefs.putString("mla", cfg.micLongAction);
  prefs.putString("mda", cfg.micDoubleAction);
  prefs.putString("vupla", cfg.volUpLongAction);
  prefs.putString("vupda", cfg.volUpDoubleAction);
  prefs.putString("vdnla", cfg.volDownLongAction);
  prefs.putString("vdnda", cfg.volDownDoubleAction);
  prefs.putFloat("micgain", cfg.micGain);
  prefs.putFloat("pcmboost", cfg.pcmVolumeBoost);
  prefs.putUShort("bdbp", cfg.buttonPressDebounceMs);
  prefs.putUShort("bdbr", cfg.buttonReleaseDebounceMs);
  prefs.putUShort("bhold", cfg.buttonHoldMs);
  prefs.putUShort("bsdelay", cfg.buttonStartupDelayMs);
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
  doc["audio"]["wakenet_enabled"] = cfg.wakenetEnabled;
  doc["audio"]["wakenet_model"] = cfg.wakenetModel;
  doc["audio"]["wakenet_supported"] = vbot_wakenet_is_supported();
  doc["audio"]["aec_supported"] = vbot_aec_is_active();
  doc["audio"]["barge_in_supported"] = vbot_wakenet_is_supported();
  doc["audio"]["psram_active"] = psramFound();
  doc["audio"]["wakenet_model_name"] = vbot_wakenet_get_model_name((WakeNetModelId)cfg.wakenetModel);
  doc["audio"]["hotword_mode"] = cfg.wakenetEnabled ? "wakenet" : (cfg.hotwordStreamEnabled ? "server" : "none");
  {
    JsonArray fillModelsArr = doc["audio"]["wakenet_models"].to<JsonArray>();
    for (uint8_t m = 0; m < WAKENET_DEFAULT_MODEL_COUNT; m++) {
      JsonObject obj = fillModelsArr.add<JsonObject>();
      obj["id"] = m;
      obj["name"] = vbot_wakenet_get_model_name((WakeNetModelId)m);
      obj["key"] = vbot_wakenet_get_model_key((WakeNetModelId)m);
      obj["is_custom"] = false;
    }
    for (uint8_t s = 0; s < WAKENET_MAX_CUSTOM_MODELS; s++) {
      WakeNetCustomSlot cslot;
      if (vbot_wakenet_get_custom_slot(s, cslot) && cslot.active) {
        JsonObject obj = fillModelsArr.add<JsonObject>();
        obj["id"] = WAKENET_DEFAULT_MODEL_COUNT + s;
        obj["name"] = cslot.name;
        obj["key"] = cslot.key;
        obj["is_custom"] = true;
        obj["slot"] = s;
      }
    }
    JsonArray fillCustomArr = doc["audio"]["custom_hotwords"].to<JsonArray>();
    for (uint8_t s = 0; s < WAKENET_MAX_CUSTOM_MODELS; s++) {
      WakeNetCustomSlot cslot;
      if (vbot_wakenet_get_custom_slot(s, cslot)) {
        JsonObject obj = fillCustomArr.add<JsonObject>();
        obj["slot"] = s;
        obj["active"] = cslot.active;
        if (cslot.active) {
          obj["name"] = cslot.name;
          obj["key"] = cslot.key;
          obj["size"] = cslot.size;
        }
      }
    }
    doc["audio"]["custom_hotwords_count"] = vbot_wakenet_get_custom_count();
    doc["audio"]["custom_hotwords_max"] = WAKENET_MAX_CUSTOM_MODELS;
  }
  doc["audio"]["mic_vad_enabled"] = cfg.micVadEnabled;
  doc["audio"]["conversation_mode"] = cfg.conversationMode;
  doc["audio"]["startup_sound_enabled"] = cfg.startupSoundEnabled;
  doc["audio"]["mic_gain"] = cfg.micGain;
  doc["audio"]["pcm_volume_boost"] = cfg.pcmVolumeBoost;
  doc["audio"]["ns_enabled"] = cfg.nsEnabled;
  doc["audio"]["agc_enabled"] = cfg.agcEnabled;
  doc["audio"]["agc_gain"] = cfg.agcGain;
  doc["logs"]["serial"] = cfg.serialLogs;
  doc["system"]["dfs_enabled"] = cfg.dfsEnabled;
  doc["network"]["mdns_enabled"] = cfg.mdnsEnabled;
  doc["network"]["dlna_enabled"] = cfg.dlnaEnabled;
  doc["network"]["airplay_enabled"] = cfg.airplayEnabled;
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
  doc["led"]["error_color"] = cfg.ledErrorColor;
  doc["led"]["speak_effect"] = cfg.ledSpeakEffect;
  doc["led"]["think_effect"] = cfg.ledThinkEffect;
  doc["led"]["mute_effect"] = cfg.ledMuteEffect;
  doc["led"]["error_effect"] = cfg.ledErrorEffect;
  doc["button"]["active"] = cfg.buttonsEnabled;
  doc["button"]["input_mode"] = cfg.inputMode;
  doc["button"]["active_level"] = cfg.buttonActiveLevel;
  doc["button"]["mic"] = cfg.pinBtnMic;
  doc["button"]["volume_up"] = cfg.pinBtnVolUp;
  doc["button"]["volume_down"] = cfg.pinBtnVolDown;
  doc["button"]["wakeup"] = cfg.pinBtnWake;
  doc["button"]["wakeup_long_action"] = cfg.wakeLongAction;
  doc["button"]["wakeup_double_action"] = cfg.wakeDoubleAction;
  doc["button"]["mic_long_action"] = cfg.micLongAction;
  doc["button"]["mic_double_action"] = cfg.micDoubleAction;
  doc["button"]["vol_up_long_action"] = cfg.volUpLongAction;
  doc["button"]["vol_up_double_action"] = cfg.volUpDoubleAction;
  doc["button"]["vol_down_long_action"] = cfg.volDownLongAction;
  doc["button"]["vol_down_double_action"] = cfg.volDownDoubleAction;
  doc["button"]["press_debounce_ms"] = cfg.buttonPressDebounceMs;
  doc["button"]["release_debounce_ms"] = cfg.buttonReleaseDebounceMs;
  doc["button"]["hold_ms"] = cfg.buttonHoldMs;
  doc["button"]["startup_delay_ms"] = cfg.buttonStartupDelayMs;
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
  doc["bt_slave"]["enabled"] = cfg.btSlaveEnabled;
  doc["bt_slave"]["bclk"] = cfg.btSlaveBclk;
  doc["bt_slave"]["ws"] = cfg.btSlaveWs;
  doc["bt_slave"]["din"] = cfg.btSlaveDin;
  doc["bt_slave"]["tx"] = cfg.btSlaveTx;
  doc["bt_slave"]["rx"] = cfg.btSlaveRx;
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
  if (doc["audio"]["hotword_mode"].is<const char *>()) {
    const char *m = doc["audio"]["hotword_mode"];
    if (strcmp(m, "wakenet") == 0) {
      cfg.wakenetEnabled = psramFound() && vbot_wakenet_is_supported();
      cfg.hotwordStreamEnabled = false;
    } else if (strcmp(m, "server") == 0) {
      cfg.wakenetEnabled = false;
      cfg.hotwordStreamEnabled = true;
    } else {
      cfg.wakenetEnabled = false;
      cfg.hotwordStreamEnabled = false;
    }
  } else {
    if (doc["audio"]["hotword_stream_enabled"].is<bool>()) cfg.hotwordStreamEnabled = doc["audio"]["hotword_stream_enabled"];
    if (doc["audio"]["wakenet_enabled"].is<bool>()) cfg.wakenetEnabled = doc["audio"]["wakenet_enabled"] && psramFound() && vbot_wakenet_is_supported();
  }
  if (doc["audio"]["wakenet_model"].is<int>()) cfg.wakenetModel = constrain((int)doc["audio"]["wakenet_model"], 0, WAKENET_MODEL_COUNT - 1);
  if (doc["audio"]["mic_vad_enabled"].is<bool>()) cfg.micVadEnabled = doc["audio"]["mic_vad_enabled"];
  if (doc["audio"]["conversation_mode"].is<bool>()) cfg.conversationMode = doc["audio"]["conversation_mode"];
  if (doc["audio"]["startup_sound_enabled"].is<bool>()) cfg.startupSoundEnabled = doc["audio"]["startup_sound_enabled"];
  if (doc["audio"]["mic_gain"].is<float>()) cfg.micGain = constrain((float)doc["audio"]["mic_gain"], 0.1f, 16.0f);
  if (doc["audio"]["pcm_volume_boost"].is<float>()) cfg.pcmVolumeBoost = constrain((float)doc["audio"]["pcm_volume_boost"], 0.1f, 16.0f);
  if (doc["logs"]["serial"].is<bool>()) cfg.serialLogs = doc["logs"]["serial"];
  if (doc["network"]["mdns_enabled"].is<bool>()) cfg.mdnsEnabled = doc["network"]["mdns_enabled"];
  if (doc["network"]["dlna_enabled"].is<bool>()) cfg.dlnaEnabled = doc["network"]["dlna_enabled"];
  if (doc["network"]["airplay_enabled"].is<bool>()) cfg.airplayEnabled = doc["network"]["airplay_enabled"];
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
  if (doc["led"]["error_color"].is<const char *>()) strlcpy(cfg.ledErrorColor, doc["led"]["error_color"], sizeof(cfg.ledErrorColor));
  if (doc["led"]["error_effect"].is<int>()) cfg.ledErrorEffect = constrain((int)doc["led"]["error_effect"], 0, 3);
  if (doc["button"]["active"].is<bool>()) cfg.buttonsEnabled = doc["button"]["active"];
  if (doc["button"]["input_mode"].is<const char *>()) {
    const char *mode = doc["button"]["input_mode"];
    strlcpy(cfg.inputMode, mode && !strcmp(mode, "rotary") ? "rotary" : "button", sizeof(cfg.inputMode));
  }
  if (doc["button"]["active_level"].is<const char *>()) {
    const char *lvl = doc["button"]["active_level"];
    strlcpy(cfg.buttonActiveLevel, lvl && !strcmp(lvl, "high") ? "high" : "low", sizeof(cfg.buttonActiveLevel));
  }
  if (doc["button"]["mic"].is<int>()) cfg.pinBtnMic = doc["button"]["mic"];
  if (doc["button"]["volume_up"].is<int>()) cfg.pinBtnVolUp = doc["button"]["volume_up"];
  if (doc["button"]["volume_down"].is<int>()) cfg.pinBtnVolDown = doc["button"]["volume_down"];
  if (doc["button"]["wakeup"].is<int>()) cfg.pinBtnWake = doc["button"]["wakeup"];
  if (doc["button"]["wakeup_long_action"].is<const char *>()) strlcpy(cfg.wakeLongAction, doc["button"]["wakeup_long_action"], sizeof(cfg.wakeLongAction));
  if (doc["button"]["wakeup_double_action"].is<const char *>()) strlcpy(cfg.wakeDoubleAction, doc["button"]["wakeup_double_action"], sizeof(cfg.wakeDoubleAction));
  if (doc["button"]["mic_long_action"].is<const char *>()) strlcpy(cfg.micLongAction, doc["button"]["mic_long_action"], sizeof(cfg.micLongAction));
  if (doc["button"]["mic_double_action"].is<const char *>()) strlcpy(cfg.micDoubleAction, doc["button"]["mic_double_action"], sizeof(cfg.micDoubleAction));
  if (doc["button"]["vol_up_long_action"].is<const char *>()) strlcpy(cfg.volUpLongAction, doc["button"]["vol_up_long_action"], sizeof(cfg.volUpLongAction));
  if (doc["button"]["vol_up_double_action"].is<const char *>()) strlcpy(cfg.volUpDoubleAction, doc["button"]["vol_up_double_action"], sizeof(cfg.volUpDoubleAction));
  if (doc["button"]["vol_down_long_action"].is<const char *>()) strlcpy(cfg.volDownLongAction, doc["button"]["vol_down_long_action"], sizeof(cfg.volDownLongAction));
  if (doc["button"]["vol_down_double_action"].is<const char *>()) strlcpy(cfg.volDownDoubleAction, doc["button"]["vol_down_double_action"], sizeof(cfg.volDownDoubleAction));
  if (doc["button"]["press_debounce_ms"].is<int>()) cfg.buttonPressDebounceMs = constrain((int)doc["button"]["press_debounce_ms"], 10, 1000);
  if (doc["button"]["release_debounce_ms"].is<int>()) cfg.buttonReleaseDebounceMs = constrain((int)doc["button"]["release_debounce_ms"], 10, 1000);
  if (doc["button"]["hold_ms"].is<int>()) cfg.buttonHoldMs = constrain((int)doc["button"]["hold_ms"], 300, 10000);
  if (doc["button"]["startup_delay_ms"].is<int>()) cfg.buttonStartupDelayMs = constrain((int)doc["button"]["startup_delay_ms"], 0, 10000);
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
  if (doc["bt_slave"]["enabled"].is<bool>()) cfg.btSlaveEnabled = doc["bt_slave"]["enabled"];
  if (doc["bt_slave"]["bclk"].is<int>()) cfg.btSlaveBclk = doc["bt_slave"]["bclk"];
  if (doc["bt_slave"]["ws"].is<int>()) cfg.btSlaveWs = doc["bt_slave"]["ws"];
  if (doc["bt_slave"]["din"].is<int>()) cfg.btSlaveDin = doc["bt_slave"]["din"];
  if (doc["bt_slave"]["tx"].is<int>()) cfg.btSlaveTx = doc["bt_slave"]["tx"];
  if (doc["bt_slave"]["rx"].is<int>()) cfg.btSlaveRx = doc["bt_slave"]["rx"];
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
    cfg.dacBclk, cfg.dacLrc, cfg.dacDout,
    strcmp(cfg.buttonActiveLevel, "high") == 0
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

static const char *manualLedEffectName(ManualLedEffect effect) {
  switch (effect) {
    case MANUAL_LED_BREATHE: return "breathe";
    case MANUAL_LED_COLOR_LOOP: return "color_loop";
    case MANUAL_LED_PULSE: return "pulse";
    case MANUAL_LED_RAINBOW: return "rainbow";
    case MANUAL_LED_COLOR_WIPE: return "color_wipe";
    case MANUAL_LED_CHASE: return "chase";
    case MANUAL_LED_THEATER_CHASE: return "theater_chase";
    case MANUAL_LED_TWINKLE: return "twinkle";
    case MANUAL_LED_SPARKLE: return "sparkle";
    case MANUAL_LED_SCANNER: return "scanner";
    case MANUAL_LED_FIRE: return "fire";
    case MANUAL_LED_CANDLE: return "candle";
    case MANUAL_LED_STROBE: return "strobe";
    default: return "solid";
  }
}

static const char *manualLedEffectLabel(ManualLedEffect effect) {
  switch (effect) {
    case MANUAL_LED_BREATHE: return "Breathe";
    case MANUAL_LED_COLOR_LOOP: return "Color Loop";
    case MANUAL_LED_PULSE: return "Pulse";
    case MANUAL_LED_RAINBOW: return "Rainbow";
    case MANUAL_LED_COLOR_WIPE: return "Color Wipe";
    case MANUAL_LED_CHASE: return "Chase";
    case MANUAL_LED_THEATER_CHASE: return "Theater Chase";
    case MANUAL_LED_TWINKLE: return "Twinkle";
    case MANUAL_LED_SPARKLE: return "Sparkle";
    case MANUAL_LED_SCANNER: return "Scanner";
    case MANUAL_LED_FIRE: return "Fire";
    case MANUAL_LED_CANDLE: return "Candle";
    case MANUAL_LED_STROBE: return "Strobe";
    default: return "Solid";
  }
}

static bool manualLedEffectFromString(String name, ManualLedEffect &effect) {
  name.trim();
  name.toLowerCase();
  name.replace(" ", "_");
  if (name == "solid") effect = MANUAL_LED_SOLID;
  else if (name == "breathe") effect = MANUAL_LED_BREATHE;
  else if (name == "color_loop") effect = MANUAL_LED_COLOR_LOOP;
  else if (name == "pulse") effect = MANUAL_LED_PULSE;
  else if (name == "rainbow") effect = MANUAL_LED_RAINBOW;
  else if (name == "color_wipe") effect = MANUAL_LED_COLOR_WIPE;
  else if (name == "chase" || name == "running" || name == "meteor") effect = MANUAL_LED_CHASE;
  else if (name == "theater_chase") effect = MANUAL_LED_THEATER_CHASE;
  else if (name == "twinkle") effect = MANUAL_LED_TWINKLE;
  else if (name == "sparkle") effect = MANUAL_LED_SPARKLE;
  else if (name == "scanner" || name == "cylon" || name == "knight_rider") effect = MANUAL_LED_SCANNER;
  else if (name == "fire") effect = MANUAL_LED_FIRE;
  else if (name == "candle") effect = MANUAL_LED_CANDLE;
  else if (name == "strobe" || name == "blink" || name == "flash") effect = MANUAL_LED_STROBE;
  else return false;
  return true;
}

static ManualLedState manualLedSnapshot() {
  ManualLedState snapshot;
  portENTER_CRITICAL(&manualLedMux);
  snapshot = manualLedState;
  portEXIT_CRITICAL(&manualLedMux);
  return snapshot;
}

static String buildManualLedStateJson() {
  ManualLedState state = manualLedSnapshot();
  AppConfig cfgSnapshot;
  lockCfg();
  cfgSnapshot = cfg;
  unlockCfg();
  JsonDocument doc;
  doc["on"] = state.on;
  doc["rgb_color"][0] = state.red;
  doc["rgb_color"][1] = state.green;
  doc["rgb_color"][2] = state.blue;
  doc["brightness"] = state.brightness;
  doc["effect"] = manualLedEffectName(state.effect);
  doc["effects"]["solid"] = manualLedEffectLabel(MANUAL_LED_SOLID);
  doc["effects"]["breathe"] = manualLedEffectLabel(MANUAL_LED_BREATHE);
  doc["effects"]["pulse"] = manualLedEffectLabel(MANUAL_LED_PULSE);
  doc["effects"]["color_loop"] = manualLedEffectLabel(MANUAL_LED_COLOR_LOOP);
  doc["effects"]["rainbow"] = manualLedEffectLabel(MANUAL_LED_RAINBOW);
  doc["effects"]["color_wipe"] = manualLedEffectLabel(MANUAL_LED_COLOR_WIPE);
  doc["effects"]["chase"] = manualLedEffectLabel(MANUAL_LED_CHASE);
  doc["effects"]["theater_chase"] = manualLedEffectLabel(MANUAL_LED_THEATER_CHASE);
  doc["effects"]["twinkle"] = manualLedEffectLabel(MANUAL_LED_TWINKLE);
  doc["effects"]["sparkle"] = manualLedEffectLabel(MANUAL_LED_SPARKLE);
  doc["effects"]["scanner"] = manualLedEffectLabel(MANUAL_LED_SCANNER);
  doc["effects"]["fire"] = manualLedEffectLabel(MANUAL_LED_FIRE);
  doc["effects"]["candle"] = manualLedEffectLabel(MANUAL_LED_CANDLE);
  doc["effects"]["strobe"] = manualLedEffectLabel(MANUAL_LED_STROBE);
  doc["supported"] = cfgSnapshot.ledEnabled && hardwareGpioValid && leds != nullptr;
  String out;
  serializeJson(doc, out);
  return out;
}

static bool applyManualLedCommand(JsonObjectConst command, String &error) {
  lockCfg();
  bool ledEnabled = cfg.ledEnabled;
  unlockCfg();
  if (!hardwareGpioValid || !ledEnabled || !leds) {
    error = "LED không được kích hoạt hoặc GPIO chưa sẵn sàng";
    return false;
  }
  String action = command["action"] | "set";
  action.trim();
  action.toLowerCase();
  if (action != "set" && action != "turn_on" && action != "turn_off") {
    error = "action chỉ nhận set, turn_on hoặc turn_off";
    return false;
  }

  ManualLedState next = manualLedSnapshot();
  if (action == "turn_on") next.on = true;
  else if (action == "turn_off") next.on = false;
  else if (!command["on"].isNull()) {
    if (!command["on"].is<bool>()) {
      error = "on phải là true hoặc false";
      return false;
    }
    next.on = command["on"].as<bool>();
  }

  if (!command["rgb_color"].isNull()) {
    if (!command["rgb_color"].is<JsonArrayConst>()) {
      error = "rgb_color phải là mảng [R,G,B]";
      return false;
    }
    JsonArrayConst color = command["rgb_color"].as<JsonArrayConst>();
    if (color.size() != 3) {
      error = "rgb_color phải có đúng ba kênh";
      return false;
    }
    uint8_t channels[3];
    for (uint8_t i = 0; i < 3; ++i) {
      if (!color[i].is<int>()) {
        error = "Mỗi kênh rgb_color phải là số nguyên từ 0 đến 255";
        return false;
      }
      int value = color[i].as<int>();
      if (value < 0 || value > 255) {
        error = "Mỗi kênh rgb_color phải nằm trong khoảng 0 đến 255";
        return false;
      }
      channels[i] = (uint8_t)value;
    }
    next.red = channels[0];
    next.green = channels[1];
    next.blue = channels[2];
    if (command["effect"].isNull()) {
      if (next.effect == MANUAL_LED_COLOR_LOOP || next.effect == MANUAL_LED_RAINBOW) {
        next.effect = MANUAL_LED_SOLID;
      }
    }
  }

  if (!command["brightness"].isNull()) {
    if (!command["brightness"].is<int>()) {
      error = "brightness phải là số nguyên từ 0 đến 255";
      return false;
    }
    int value = command["brightness"].as<int>();
    if (value < 0 || value > 255) {
      error = "brightness phải nằm trong khoảng 0 đến 255";
      return false;
    }
    next.brightness = (uint8_t)value;
  }

  if (!command["effect"].isNull()) {
    if (!command["effect"].is<const char *>()) {
      error = "effect phải là chuỗi";
      return false;
    }
    String effectName = command["effect"].as<String>();
    if (!manualLedEffectFromString(effectName, next.effect)) {
      error = "effect chỉ nhận solid, breathe, pulse, color_loop, rainbow, color_wipe, chase, theater_chase, twinkle, sparkle, scanner, fire, candle hoặc strobe";
      return false;
    }
  }

  portENTER_CRITICAL(&manualLedMux);
  manualLedState = next;
  portEXIT_CRITICAL(&manualLedMux);
  ledRefreshRequested = true;
  mqttStateDirty = true;
  LOGF("[LED Effects Control] action=%s on=%s RGB(%u,%u,%u) brightness=%u effect=%s",
       action.c_str(), next.on ? "true" : "false", next.red, next.green, next.blue,
       next.brightness, manualLedEffectName(next.effect));
  return true;
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
  fill_solid(leds, cfg.ledCount, parseHexColor(cfg.ledErrorColor, CRGB(255, 40, 20)));
  FastLED.show();
}

static void showSocketErrorBlink(uint16_t step) {
  if (!leds) return;
  FastLED.setBrightness(ledBrightness());
  CRGB color = parseHexColor(cfg.ledErrorColor, CRGB(255, 40, 20));
  fill_solid(leds, cfg.ledCount, ((step / 15) % 2 == 0) ? color : CRGB::Black);
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

static void showManualLed(const ManualLedState &state, uint16_t step) {
  if (!leds) return;
  const uint16_t count = cfg.ledCount;
  const CRGB baseColor(state.red, state.green, state.blue);
  uint16_t scaledBrightness = ((uint16_t)ledBrightness() * state.brightness + 127) / 255;
  if (state.effect == MANUAL_LED_BREATHE || state.effect == MANUAL_LED_PULSE) {
    const uint8_t pulse = state.effect == MANUAL_LED_PULSE
      ? beatsin8(30, 8, 255, 0, 0)
      : beatsin8(12, 16, 255, 0, 0);
    scaledBrightness = (scaledBrightness * pulse + 127) / 255;
  }
  FastLED.setBrightness((uint8_t)scaledBrightness);
  switch (state.effect) {
    case MANUAL_LED_COLOR_LOOP:
      fill_solid(leds, count, CHSV((uint8_t)(step * 2), 240, 255));
      break;
    case MANUAL_LED_RAINBOW:
      fill_rainbow(leds, count, (uint8_t)(step * 2), count > 1 ? (255 / count) : 32);
      break;
    case MANUAL_LED_COLOR_WIPE: {
      if (count <= 1) {
        leds[0] = (step % 20 < 10) ? baseColor : CRGB::Black;
      } else {
        uint16_t wipeStep = (step / 2) % (count * 2);
        if (wipeStep < count) {
          for (uint16_t i = 0; i <= wipeStep; ++i) leds[i] = baseColor;
          for (uint16_t i = wipeStep + 1; i < count; ++i) leds[i] = CRGB::Black;
        } else {
          uint16_t clearIdx = wipeStep - count;
          for (uint16_t i = 0; i <= clearIdx; ++i) leds[i] = CRGB::Black;
          for (uint16_t i = clearIdx + 1; i < count; ++i) leds[i] = baseColor;
        }
      }
      break;
    }
    case MANUAL_LED_CHASE: {
      if (count <= 1) {
        leds[0] = baseColor;
        leds[0].nscale8_video(beatsin8(25, 30, 255));
      } else {
        fadeToBlackBy(leds, count, 60);
        leds[step % count] = baseColor;
      }
      break;
    }
    case MANUAL_LED_THEATER_CHASE: {
      fill_solid(leds, count, CRGB::Black);
      const uint16_t offset = step % 3;
      for (uint16_t i = offset; i < count; i += 3) leds[i] = baseColor;
      break;
    }
    case MANUAL_LED_TWINKLE: {
      fill_solid(leds, count, CRGB::Black);
      uint16_t sparkles = max((uint16_t)1, (uint16_t)(count / 5));
      for (uint16_t i = 0; i < sparkles; ++i) {
        uint16_t idx = random16(count);
        leds[idx] = baseColor;
        leds[idx].nscale8_video(random8(90, 255));
      }
      break;
    }
    case MANUAL_LED_SPARKLE: {
      fill_solid(leds, count, baseColor);
      if (random8(10) < 4) {
        leds[random16(count)] = CRGB::White;
      }
      break;
    }
    case MANUAL_LED_SCANNER: {
      if (count <= 1) {
        leds[0] = baseColor;
        leds[0].nscale8_video(beatsin8(25, 20, 255));
      } else {
        fadeToBlackBy(leds, count, 70);
        uint16_t pos = beatsin16(25, 0, count - 1);
        leds[pos] = baseColor;
      }
      break;
    }
    case MANUAL_LED_FIRE:
      for (uint16_t i = 0; i < count; ++i) {
        leds[i] = baseColor;
        leds[i].nscale8_video(random8(60, 255));
      }
      break;
    case MANUAL_LED_CANDLE:
      for (uint16_t i = 0; i < count; ++i) {
        uint8_t flicker = random8(0, 50);
        CRGB c = baseColor;
        c.nscale8_video(255 - flicker);
        leds[i] = c;
      }
      break;
    case MANUAL_LED_STROBE:
      fill_solid(leds, count, (step % 6 < 3) ? baseColor : CRGB::Black);
      break;
    default:
      fill_solid(leds, count, baseColor);
      break;
  }
  FastLED.show();
}

static void showVuMeter(uint16_t step) {
  if (!leds || cfg.ledCount == 0) return;
  uint16_t count = cfg.ledCount;
  
  static float smoothPeak = 0.0f;
  static float peakHold = 0.0f;
  static uint32_t peakHoldMs = 0;
  
  uint16_t p = currentAudioPeak;
  currentAudioPeak = 0;
  
  if ((float)p > smoothPeak) {
    smoothPeak = smoothPeak * 0.35f + (float)p * 0.65f;
  } else {
    smoothPeak = smoothPeak * 0.80f;
  }
  
  uint32_t now = millis();
  if (smoothPeak > peakHold) {
    peakHold = smoothPeak;
    peakHoldMs = now + 250;
  } else if (now > peakHoldMs) {
    peakHold = peakHold * 0.90f;
  }
  
  int lit = map((long)smoothPeak, 200, 14000, 0, (long)count);
  lit = constrain(lit, 0, (int)count);
  
  int peakIdx = map((long)peakHold, 200, 14000, 0, (long)(count - 1));
  peakIdx = constrain(peakIdx, 0, (int)(count - 1));

  uint8_t baseHue = (step * 3) & 0xFF; // Tự động đổi màu cầu vồng đa sắc chuyển động liên tục
  for (uint16_t i = 0; i < count; i++) {
    if (i < (uint16_t)lit) {
      uint8_t hue = baseHue + (i * 256 / count);
      leds[i] = CHSV(hue, 255, 255);
    } else {
      leds[i] = CRGB::Black;
    }
  }
  if (peakIdx >= 0 && peakIdx < count && lit > 0) {
    leds[peakIdx] = CHSV((baseHue + 128) & 0xFF, 180, 255);
  }
  FastLED.show();
}

static void showAudioReactivePulse(uint16_t step) {
  if (!leds || cfg.ledCount == 0) return;
  uint16_t count = cfg.ledCount;
  
  static float smoothPeak = 0.0f;
  uint16_t p = currentAudioPeak;
  currentAudioPeak = 0;
  
  if ((float)p > smoothPeak) {
    smoothPeak = smoothPeak * 0.4f + (float)p * 0.6f;
  } else {
    smoothPeak = smoothPeak * 0.85f;
  }
  
  uint8_t brightness = map((long)smoothPeak, 300, 22000, 25, 255);
  uint8_t baseHue = (step * 2) & 0xFF;
  for (uint16_t i = 0; i < count; i++) {
    uint8_t h = baseHue + (i * 256 / count);
    leds[i] = CHSV(h, 240, brightness);
  }
  FastLED.show();
}

static void showAudioReactiveFire(uint16_t step) {
  if (!leds || cfg.ledCount == 0) return;
  uint16_t count = cfg.ledCount;
  
  static float smoothPeak = 0.0f;
  uint16_t p = currentAudioPeak;
  currentAudioPeak = 0;
  if ((float)p > smoothPeak) smoothPeak = (float)p;
  else smoothPeak = smoothPeak * 0.85f;
  
  uint8_t boost = map((long)smoothPeak, 400, 24000, 0, 100);

  for (uint16_t i = 0; i < count; i++) {
    uint8_t heat = qadd8(random8(50, 150), boost);
    leds[i] = HeatColor(heat);
  }
  FastLED.show();
}

static void showSpinningComet(uint16_t step, CRGB color) {
  if (!leds || cfg.ledCount == 0) return;
  uint16_t count = cfg.ledCount;
  uint16_t head = step % count;
  fadeToBlackBy(leds, count, 60);
  leds[head] = color;
  uint16_t prev = (head + count - 1) % count;
  leds[prev] += CRGB(color.r / 3, color.g / 3, color.b / 3);
  FastLED.show();
}

static void showBreatheEffect(uint16_t step, CRGB color) {
  if (!leds || cfg.ledCount == 0) return;
  uint16_t count = cfg.ledCount;
  uint8_t b = beatsin8(25, 20, 255);
  CRGB c = color;
  c.nscale8_video(b);
  fill_solid(leds, count, c);
  FastLED.show();
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
    if ((vbot_airplay_is_streaming() || vbot_bt_slave_is_streaming()) && !volumeLedActive) {
      effect = LED_SPEAK;
    }
    ManualLedState manual = manualLedSnapshot();
    if (manual.on && (effect == LED_OFF || effect == LED_MUTE)) {
      showManualLed(manual, step);
      lastStaticEffect = (LedEffect)255;
      ++step;
      vTaskDelay(pdMS_TO_TICKS(manual.effect == MANUAL_LED_SOLID ? 50 : 20));
      continue;
    }
    bool animated = effect == LED_SPEAK || effect == LED_LOADING ||
                    (effect == LED_MUTE && cfg.ledMuteEffect != 0) ||
                    effect == LED_ERROR ||
                    (effect == LED_SOCKET_ERROR && cfg.ledErrorEffect != 1) ||
                    effect == LED_VOLUME ||
                    effect == LED_STARTUP ||
                    (effect == LED_THINK && cfg.ledThinkEffect != 0);
    if (!animated && effect == lastStaticEffect) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    switch (effect) {
      case LED_SPEAK:
        if (cfg.ledSpeakEffect == 1) showVuMeter(step);
        else if (cfg.ledSpeakEffect == 2) showAudioReactivePulse(step);
        else if (cfg.ledSpeakEffect == 3) showAudioReactiveFire(step);
        else showRainbow(step, 4);
        break;
      case LED_THINK:
        if (cfg.ledThinkEffect == 1) showSpinningComet(step, parseHexColor(cfg.ledThinkColor, CRGB::Blue));
        else if (cfg.ledThinkEffect == 2) showBreatheEffect(step, parseHexColor(cfg.ledThinkColor, CRGB::Blue));
        else showSolid(parseHexColor(cfg.ledThinkColor, CRGB::Blue));
        break;
      case LED_LOADING: showSpinningComet(step, CRGB::Cyan); break;
      case LED_MUTE:
        if (cfg.ledMuteEffect == 1) showMuteBreath(step);
        else if (cfg.ledMuteEffect == 2) showSpinningComet(step, parseHexColor(cfg.ledMuteColor, CRGB(255, 140, 0)));
        else showSolid(parseHexColor(cfg.ledMuteColor, CRGB(255, 140, 0)));
        break;
      case LED_ERROR: showSolid((step % 50) < 25 ? CRGB::Red : CRGB::Black); break;
      case LED_SOCKET_ERROR:
        if (cfg.ledErrorEffect == 1) showSolid(parseHexColor(cfg.ledErrorColor, CRGB(255, 40, 20)));
        else if (cfg.ledErrorEffect == 2) showSocketErrorBlink(step);
        else if (cfg.ledErrorEffect == 3) showSpinningComet(step, parseHexColor(cfg.ledErrorColor, CRGB(255, 40, 20)));
        else showSocketErrorBreath(step);
        break;
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

static uint32_t buttonReadyAtMs = 0;

static void initButtons() {
  if (!cfg.buttonsEnabled) return;
  buttonReadyAtMs = millis() + cfg.buttonStartupDelayMs;
  if (inputModeRotary()) {
    if (cfg.pinRotaryClk >= 0) pinMode(cfg.pinRotaryClk, INPUT_PULLUP);
    if (cfg.pinRotaryDt >= 0) pinMode(cfg.pinRotaryDt, INPUT_PULLUP);
    if (cfg.pinRotarySw >= 0) pinMode(cfg.pinRotarySw, INPUT_PULLUP);
    return;
  }
  uint8_t mode = (strcmp(cfg.buttonActiveLevel, "high") == 0) ? INPUT_PULLDOWN : INPUT_PULLUP;
  if (cfg.pinBtnMic >= 0) pinMode(cfg.pinBtnMic, mode);
  if (cfg.pinBtnVolUp >= 0) pinMode(cfg.pinBtnVolUp, mode);
  if (cfg.pinBtnVolDown >= 0) pinMode(cfg.pinBtnVolDown, mode);
  if (cfg.pinBtnWake >= 0) pinMode(cfg.pinBtnWake, mode);
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
  if (vbot_airplay_is_streaming() || vbot_bt_slave_is_streaming()) return;
  if (pcmTxInstalled && currentPcmSampleRate == 16000) {
    i2s_zero_dma_buffer(PCM_I2S_PORT);
    return;
  }
  uninstallPcmTx();
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
  currentPcmSampleRate = 16000;
  pcmTxInstalled = true;
}

static void stopMp3(bool keepSilentDac) {
  vbot_aec_set_playback_active(false);
  if (mp3Out) {
    mp3Out->SetGain(0.0f);
  }
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
  currentPcmSampleRate = sampleRate;
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
    if (mp3PsramStreamBuffer) {
      mp3File = new (std::nothrow) AudioFileSourceBuffer(mp3RawFile, mp3PsramStreamBuffer, MP3_STREAM_BUFFER_SIZE_PSRAM);
    } else {
      mp3File = new (std::nothrow) AudioFileSourceBuffer(mp3RawFile, MP3_STREAM_BUFFER_SIZE);
    }
  }
  if (!mp3File || !mp3File->isOpen()) {
    LOGF("Không mở được MP3 %s: %s", localFile ? "từ bộ nhớ LittleFS" : "từ URL", source);
    stopMp3();
    //setLedEffect(LED_ERROR);
    if (feedbackOnFail && !strstr(source, "dong.mp3")) playMp3FilePath("/sound/dong.mp3");
    audioOpening = false;
    resumeMicAfterAudio();
    return false;
  }
  uint32_t targetWarmupMs = localFile ? 30 : DAC_MP3_WARMUP_MS;
  uint32_t warmupElapsed = millis() - dacWarmupStarted;
  if (warmupElapsed < targetWarmupMs) {
    vTaskDelay(pdMS_TO_TICKS(targetWarmupMs - warmupElapsed));
  }
  // AudioOutputI2S sẽ cài và sở hữu driver I2S trong thời gian giải mã.
  // Chỉ gỡ driver DAC im lặng. Source MP3 vừa mở phải còn nguyên cho begin().
  uninstallPcmTx();
  //LOGF("Đã mở source MP3: size=%u, free=%u, min=%u", (unsigned)mp3File->getSize(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
  mp3Out = new (std::nothrow) AudioOutputVBotAEC(0, AudioOutputI2S::EXTERNAL_I2S, psramFound() ? 8 : 6);
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
  if (mp3PreallocBuffer) {
    mp3 = new (std::nothrow) AudioGeneratorMP3(mp3PreallocBuffer, (int)MP3_PREALLOC_SIZE);
  } else {
    mp3 = new (std::nothrow) AudioGeneratorMP3();
  }
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
  uint32_t targetOutputWarmupMs = localFile ? 20 : DAC_MP3_WARMUP_MS;
  uint32_t outputWarmupDeadline = millis() + targetOutputWarmupMs;
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
  i2sConfig.dma_buf_count = 4;
  i2sConfig.dma_buf_len = FRAME_SAMPLES;
  i2sConfig.use_apll = false;
  i2sConfig.tx_desc_auto_clear = false;
  i2s_pin_config_t pinConfig = {};
  pinConfig.mck_io_num = I2S_PIN_NO_CHANGE;
  pinConfig.bck_io_num = cfg.micBclk;
  pinConfig.ws_io_num = cfg.micWs;
  pinConfig.data_out_num = I2S_PIN_NO_CHANGE;
  pinConfig.data_in_num = cfg.micDin;
  esp_err_t err = i2s_driver_install(MIC_I2S_PORT, &i2sConfig, 0, nullptr);
  if (err != ESP_OK) {
    LOGF("Quá trình cài đặt trình điều khiển MIC I2S thất bại: %d", err);
    unlockMicI2s();
    return;
  }
  err = i2s_set_pin(MIC_I2S_PORT, &pinConfig);
  if (err != ESP_OK) {
    LOGF("Lỗi thiết lập chân MIC I2S: %d", err);
    i2s_driver_uninstall(MIC_I2S_PORT);
    micRxInstalled = false;
    unlockMicI2s();
    return;
  }
  i2s_zero_dma_buffer(MIC_I2S_PORT);
  micRxInstalled = true;
  LOGF("[MIC-INIT] Khởi tạo MIC I2S thành công: PORT=%d, BCLK=GPIO%d, WS=GPIO%d, DIN=GPIO%d, Kênh=%s, Gain=%.2f",
       MIC_I2S_PORT, cfg.micBclk, cfg.micWs, cfg.micDin, cfg.micChannel, cfg.micGain);
  unlockMicI2s();
}

static void suspendMicForAudio() {
  if (cfg.wakenetEnabled) return; // Cho phép mic tiếp tục chạy để nhận diện từ khóa và ngắt lời (Barge-in)
  if (!micRxInstalled) return;
  micSuspendedForAudio = true;
  lockMicI2s();
  if (micRxInstalled) {
    i2s_zero_dma_buffer(MIC_I2S_PORT);
  }
  unlockMicI2s();
}

static void resumeMicAfterAudio() {
  if (!vbotServerStartupEnabled() || !micSuspendedForAudio || audioOpening) return;
  lockMicI2s();
  if (micRxInstalled) {
    i2s_zero_dma_buffer(MIC_I2S_PORT);
  }
  unlockMicI2s();
  micSuspendedForAudio = false;
}

static void resetMicVadState() {
  vadPreRollWriteIdx = 0;
  vadPreRollCount = 0;
  vadHoldUntilMs = 0;
  micVadGateOpen = false;
}

static void enqueueMicFrameDropOldest(const MicFrame &frame) {
  if (!micQueue) return;
  if (xQueueSend(micQueue, &frame, 0) != pdTRUE) {
    xQueueReceive(micQueue, &micDropFrame, 0);
    xQueueSend(micQueue, &frame, 0);
  }
}

static bool readMicFrame(MicFrame &frame, int32_t *outAvg = nullptr, int32_t *outPeak = nullptr) {
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
  int32_t frameAvg = (int32_t)(absSum / FRAME_SAMPLES);
  if (outAvg) *outAvg = frameAvg;
  if (outPeak) *outPeak = peak;
  return true;
}

static bool audioIdle();
static bool stopAudioPlayback(bool clearSuspendedAudio);

static void micTask(void *) {
  bool wasCapturing = false;
  for (;;) {
    bool shouldCapture = !otaUpdateActive && wsReady && (!mqttNetworkBusy || recording) &&
                         micActive() && !vbot_airplay_is_streaming() && !vbot_bt_slave_is_streaming() && (audioIdle() || cfg.wakenetEnabled) &&
                         (recording || ((cfg.hotwordStreamEnabled || cfg.wakenetEnabled) && (waitingToWake || !audioIdle())));
    if (!shouldCapture) {
      if (wasCapturing) {
        wasCapturing = false;
        resetMicVadState();
      }
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    if (!wasCapturing) {
      // Xóa sạch bộ đệm DMA I2S RX và bỏ 1 frame đầu tiên ngay sau khi loa vừa
      // phát xong (PCM/MP3) để tiếng vọng cuối câu của loa không lọt vào Mic.
      lockMicI2s();
      if (micRxInstalled) i2s_zero_dma_buffer(MIC_I2S_PORT);
      unlockMicI2s();
      resetMicVadState();
      readMicFrame(micWorkFrame);
      wasCapturing = true;
    }

    int32_t frameAvg = 0;
    int32_t framePeak = 0;
    if (!readMicFrame(micWorkFrame, &frameAvg, &framePeak)) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    int16_t cleanFrame[FRAME_SAMPLES];
    vbot_audio_dsp_process((const int16_t*)micWorkFrame.data, cleanFrame, FRAME_SAMPLES, cfg.nsEnabled, cfg.agcEnabled);

    if (!recording && (waitingToWake || !audioIdle()) && cfg.wakenetEnabled) {
      if (vbot_wakenet_is_running()) {
        if (vbot_wakenet_feed(cleanFrame, FRAME_SAMPLES)) {
          LOG("[WakeNet] Phát hiện từ khóa đánh thức!");
          if (!audioIdle()) {
            LOG("[Barge-In] Ngắt lời thành công khi loa đang phát! Dừng phát audio ngay lập tức.");
            stopAudioPlayback(true);
          }
          wakeSkipRequest = true;
        }
        if (!cfg.hotwordStreamEnabled && audioIdle()) {
          continue;
        }
      }
    }

    // Cập nhật khung âm thanh sạch (đã qua AEC, NS, AGC) để gửi lên máy chủ STT
    memcpy(micWorkFrame.data, cleanFrame, FRAME_BYTES);

    // Nếu đang phát âm thanh và chưa vào phiên ghi âm, không gửi âm thanh môi trường lên server
    if (!audioIdle() && !recording) {
      continue;
    }

    // Khi đang trong phiên thu âm câu lệnh (recording == true) hoặc tắt VAD,
    // gửi 100% các gói âm thanh để STT trên máy chủ xử lý đầy đủ.
    if (recording || !cfg.micVadEnabled || !vadPreRoll) {
      if (vadPreRollCount > 0) resetMicVadState();
      micVadGateOpen = true;
      enqueueMicFrameDropOldest(micWorkFrame);
      continue;
    }

    // Chế độ chờ đánh thức (waitingToWake) có bật Adaptive VAD + Pre-Roll:
    float avgTrigger = max(135.0f, vadNoiseFloorEma * 2.15f + 40.0f);
    float peakTrigger = max(520.0f, vadNoiseFloorEma * 5.2f + 180.0f);
    bool voiceDetected = ((float)frameAvg >= avgTrigger) || ((float)framePeak >= peakTrigger);

    if (!voiceDetected) {
      vadNoiseFloorEma = vadNoiseFloorEma * 0.96f + (float)frameAvg * 0.04f;
    } else if ((float)frameAvg > vadNoiseFloorEma) {
      // Thích nghi rất chậm nếu nền nhiễu phòng tăng đều (ví dụ bật quạt)
      vadNoiseFloorEma = vadNoiseFloorEma * 0.995f + (float)frameAvg * 0.005f;
    }
    vadNoiseFloorEma = constrain(vadNoiseFloorEma, 35.0f, 1800.0f);
    micVadNoiseFloor = (int32_t)vadNoiseFloorEma;

    uint32_t now = millis();
    if (voiceDetected) {
      if (!micVadGateOpen && vadPreRollCount > 0) {
        // Xả các frame nhìn trước (Pre-Roll ~128ms) theo đúng thứ tự thời gian
        // để âm đầu của câu gọi ("Ok VBot") không bao giờ bị cắt mất.
        size_t startIdx = (vadPreRollWriteIdx + VAD_PREROLL_FRAMES - vadPreRollCount) % VAD_PREROLL_FRAMES;
        for (size_t i = 0; i < vadPreRollCount; ++i) {
          size_t idx = (startIdx + i) % VAD_PREROLL_FRAMES;
          enqueueMicFrameDropOldest(vadPreRoll[idx]);
        }
        vadPreRollCount = 0;
        vadPreRollWriteIdx = 0;
      }
      micVadGateOpen = true;
      vadHoldUntilMs = now + VAD_HANGOVER_MS;
      enqueueMicFrameDropOldest(micWorkFrame);
    } else if (vadHoldUntilMs && !deadlineReached(vadHoldUntilMs, now)) {
      micVadGateOpen = true;
      enqueueMicFrameDropOldest(micWorkFrame);
    } else {
      micVadGateOpen = false;
      vadHoldUntilMs = 0;
      vadPreRoll[vadPreRollWriteIdx] = micWorkFrame;
      vadPreRollWriteIdx = (vadPreRollWriteIdx + 1) % VAD_PREROLL_FRAMES;
      if (vadPreRollCount < VAD_PREROLL_FRAMES) vadPreRollCount++;
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
         !pcmPlaybackActive && (millis() - lastPcmPlaybackMs >= 300) &&
         (!audioQueue || uxQueueMessagesWaiting(audioQueue) == 0) &&
         !vbot_airplay_is_streaming() &&
         !vbot_bt_slave_is_streaming();
}

static bool isVBotAssistantBusy() {
  if (recording) return true;
  if (!waitingToWake) return true;
  if (wakeSkipRequest) return true;
  if (otaUpdateActive) return true;
  if (audioOpening) return true;
  if (mp3 && mp3->isRunning()) return true;
  if (pcmPlaybackActive) return true;
  if (audioQueue && uxQueueMessagesWaiting(audioQueue) > 0) return true;
  if (millis() - lastPcmPlaybackMs < 600) return true;
  if (vbot_airplay_is_streaming()) return true;
  return false;
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
  if (volumeFeedbackPending && deadlineReached(volumeFeedbackAtMs, now)) {
    volumeFeedbackPending = false;
    if (cfg.volume > 0 && waitingToWake && !recording && audioIdle() && localTutTutAvailable && !vbot_bt_slave_is_streaming()) {
      playMp3FilePath("/sound/tut_tut.mp3", false, LED_SPEAK, false);
    }
  }
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
      playMp3FilePath("/sound/tut_tut.mp3", false, LED_SPEAK, false);
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
    snprintf(message, sizeof(message), "Không tạo được task %s, stack=%lu, core=%ld", name, (unsigned long)stackDepth, (long)coreId);
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
  if (length > PCM_POOL_BUFFER_SIZE || !pcmFreeQueue || !pcmPool) {
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
  uint16_t localPeak = 0;
  for (size_t i = 0; i < count; i++) {
    int64_t v = (int64_t)samples[i] * gainFixed;
    int16_t s = clamp16((int32_t)((v + 128) >> 8));
    samples[i] = s;
    int32_t a = abs((int32_t)s);
    if (a > localPeak) localPeak = (uint16_t)a;
  }
  if (localPeak > currentAudioPeak) {
    currentAudioPeak = localPeak;
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
        if ((!pcmTxInstalled || currentPcmSampleRate != cmd.sampleRate) && !installPcmTx(cmd.sampleRate)) {
          LOG("Khởi tạo DAC PCM thất bại");
          pcmPlaybackActive = false;
          setLedEffect(LED_ERROR);
        } else {
          size_t written = 0;
          //Áp dụng volume chung cho PCM realtime
          amplifyPcm16((uint8_t *)cmd.pcm, cmd.pcmLen, cfg.volume);
          if (vbot_aec_is_active()) {
            vbot_aec_feed_ref((const int16_t*)cmd.pcm, cmd.pcmLen / sizeof(int16_t));
          }
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
            if (cmd.speakLed && !recording && !volumeLedActive) setLedEffect(cmd.playLedEffect);
          }
        }
        if (cmd.pcmPoolSlot >= 0 && pcmFreeQueue) {
          uint8_t slot = (uint8_t)cmd.pcmPoolSlot;
          xQueueSend(pcmFreeQueue, &slot, 0);
        }
      }

      else if (cmd.type == AUDIO_CMD_SET_VOLUME) {
        if (mp3Out) mp3Out->SetGain((float)cfg.volume / 100.0f);
        vbot_bt_slave_set_volume(cfg.volume);
      }
      //Dừng âm thanh đang phát, bao gồm MP3 và PCM
      else if (cmd.type == AUDIO_CMD_STOP) {
        pcmPlaybackActive = false;
        vbot_aec_set_playback_active(false);
        pendingAutoContinue = false;
        bool airplayActive = vbot_airplay_is_streaming();
        bool btSlaveActive = vbot_bt_slave_is_streaming();
        stopMp3(!airplayActive && !btSlaveActive);
        lockMediaState();
        currentAudioUrl = "";
        if (googleTtsState != 0) {
          googleTtsState = 0;
          googleTtsUpdatedAtMs = millis();
        }
        unlockMediaState();
        mqttStateDirty = true;
        if (!airplayActive && !btSlaveActive) {
          resumeMicAfterAudio();
        }
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
      vbot_aec_set_playback_active(false);
      if (micQueue) xQueueReset(micQueue);
      mqttStateDirty = true;
    }
    if (pendingAutoContinue && wsReady && audioIdle()) {
      pendingAutoContinue = false;
      wakeSkipRequest = true;
      setLedEffect(LED_THINK);
    }
    if (!pendingAutoContinue && !wakeSkipRequest && !recording && (!mp3 || !mp3->isRunning()) && !pcmPlaybackRecentlyActive()
        && !vbot_airplay_is_streaming() && !vbot_bt_slave_is_streaming()
        && (ledEffect == LED_SPEAK || ledEffect == LED_STARTUP) && deadlineReached(testLedUntilMs)) {
      setLedEffect(micActive() ? LED_OFF : LED_MUTE);
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

//Gửi dữ liệu cấu hình ban đầu của client tới máy chủ VBot sau khi kết nối WebSocket được thiết lập, bao gồm cả session ID duy nhất cho phiên làm việc này và các tuỳ chọn cấu hình khác để máy chủ biết cách xử lý âm thanh và phản hồi phù hợp
static void sendClientConfig(bool createNewSession = true) {
  String out;
  lockCfg();
  String sessionPrefix = sessionPrefixFromClientName();
  strlcpy(cfg.sessionId, sessionPrefix.c_str(), sizeof(cfg.sessionId));
  if (createNewSession || activeSessionId.isEmpty()) {
    activeSessionId = sessionPrefix + "_" + String((uint32_t)ESP.getEfuseMac(), HEX) + "_" + String((uint32_t)esp_random(), HEX);
  }
  JsonDocument doc;
  doc["session_id"] = activeSessionId;
  doc["client_name"] = cfg.clientName;
  doc["working_mode"] = cfg.workingMode;
  doc["conversation_mode"] = cfg.conversationMode;
  serializeJson(doc, out);
  unlockCfg();
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
    recordingDurationMs = 8000U;
    recordingDeadlineMs = millis() + recordingDurationMs;
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
    recordingDurationMs = maximumRecordingMs;
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
    bool hasAudio = validAudioUrl(audioUrl);
    if (hasAudio) {
      if (validAudioUrl(tts)) {
        setLedEffect(LED_SPEAK);
      } else {
        setLedEffect(micActive() ? LED_OFF : LED_MUTE);
      }
      playMp3Url(audioUrl);
    } else {
      if (!pcmPlaybackRecentlyActive() && uxQueueMessagesWaiting(audioQueue) == 0) {
        setLedEffect(micActive() ? LED_OFF : LED_MUTE);
      }
    }
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
    if (hasAudio) {
      setLedEffect(LED_SPEAK);
      playMp3Url(validAudioUrl(tts) ? tts : status, cfg.conversationMode);
    } else {
      if (!pcmPlaybackRecentlyActive() && uxQueueMessagesWaiting(audioQueue) == 0) {
        setLedEffect(micActive() ? LED_OFF : LED_MUTE);
      }
      if (cfg.conversationMode) {
        // Phản hồi main_processing có thể là PCM raw. Chờ DAC phát hết hàng đợi
        // rồi mới yêu cầu lượt thu tiếp để không cắt mất phần cuối câu trả lời.
        pendingAutoContinue = true;
      }
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

static void wsEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      ws.resetPendingMicFrame();
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
      wsReady = false;
      wsBackpressureSinceMs = 0;
      pendingPcmBinary = false;
      pendingPcmMeta.clear();
      recording = false;
      recordingDeadlineMs = 0;
      waitingToWake = true;
      pendingAutoContinue = false;
      stopRecordingRequested = false;
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
  LOGF("Địa chỉ kết nối máy chủ WebSocket ws://%s:%u%s", cfg.serverHost, cfg.serverPort, cfg.serverPath);
}

static void websocketTask(void *) {
  uint32_t lastMicLog = 0;
  for (;;) {
    if (ws.hasPendingMicFrame()) {
      int micFlushResult = ws.flushPendingMicFrame();
      if (micFlushResult < 0) {
        ws.disconnect();
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
    if (stopRecordingRequested && wsReady) {
      stopRecordingRequested = false;
      ws.sendTXT("stop");
    }
    if (wakeSkipRequest && wsReady) {
      wakeSkipRequest = false;
      if (!cfg.micEnabled) temporaryMicEnabled = true;
      stopAudioPlayback(true);
      ws.sendTXT("Skip_WakeUP");
      recording = true;
      recordingDurationMs = 8000U;
      recordingDeadlineMs = millis() + recordingDurationMs;
      waitingToWake = false;
      pendingAutoContinue = false;
      LOG("Đã gửi lệnh đánh thức, đang thu âm thanh...");
      setLedEffect(LED_THINK);
    }
    if (recording && !audioIdle() && recordingDeadlineMs != 0) {
      // Giữ nguyên thời lượng thu âm cho tới khi phát xong status_audio (ding/wakeup_reply)
      recordingDeadlineMs = millis() + recordingDurationMs;
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
    const bool webUiActiveNow = isWebUiActive();
    const bool hotwordBackpressurePaused = (hotwordBackpressureUntilMs &&
      !deadlineReached(hotwordBackpressureUntilMs)) || webUiActiveNow;
    bool allowMicSend = !otaUpdateActive && (!mqttNetworkBusy || recording) && micActive() && audioIdle() &&
                        (recording || (cfg.hotwordStreamEnabled && waitingToWake &&
                                       !hotwordBackpressurePaused && ws.canSendWithoutBlocking()));
    if (!recording && waitingToWake && (!ws.canSendWithoutBlocking() || webUiActiveNow) && micQueue && uxQueueMessagesWaiting(micQueue) > 1) {
      MicFrame dummy;
      xQueueReceive(micQueue, &dummy, 0);
    }
    if (wsReady) {
      int micFlushResult = ws.flushPendingMicFrame();
      if (micFlushResult < 0) {
        ws.disconnect();
        wsBackpressureSinceMs = 0;
      } else if (ws.hasPendingMicFrame()) {
        if (!recording && webUiActiveNow && !ws.pendingMicFrameStarted()) {
          ws.resetPendingMicFrame();
          if (micQueue) xQueueReset(micQueue);
          wsBackpressureSinceMs = 0;
        } else {
          if (!wsBackpressureSinceMs) wsBackpressureSinceMs = millis();
          if (millis() - wsBackpressureSinceMs >= 5000) {
            if (!recording && !ws.pendingMicFrameStarted()) {
              ws.resetPendingMicFrame();
              if (micQueue) xQueueReset(micQueue);
              hotwordBackpressureUntilMs = millis() + 10000;
              UART_LOGF("Hotword stream tạm nghỉ 10 giây vì TCP đầy; giữ WebSocket và ưu tiên WebUI. free=%u max_block=%u",
                        (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
            } else {
              UART_LOGF("WebSocket có frame mic gửi dở quá 5 giây; đóng socket. recording=%d free=%u max_block=%u",
                        recording ? 1 : 0, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
              wsForceReconnectAtMs = millis() + WS_RECONNECT_INTERVAL_MS;
              ws.disconnect();
            }
            wsBackpressureSinceMs = 0;
          }
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
    bool wsActiveWork = recording || ws.hasPendingMicFrame() ||
                        (micQueue && uxQueueMessagesWaiting(micQueue) > 0);
    vTaskDelay(pdMS_TO_TICKS(wsActiveWork ? 2 : 5));
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
  int micBclk, int micWs, int micDin, int dacBclk, int dacLrc, int dacDout,
  bool buttonActiveHigh
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
                      "GPIO boot strap, ngoại vi có thể làm ESP không boot nếu kéo sai mức khi reset");
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
    appendGpioError(conflicts, "I2S Mic DOUT", micDin, "Không hỗ trợ input");
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
      appendGpioError(conflicts, pullupPins[i].name, pullupPins[i].pin,
                      (buttonActiveHigh && i < 4) ? "Không hỗ trợ INPUT_PULLDOWN" : "Không hỗ trợ INPUT_PULLUP");
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
  return "{\"success\":true,\"pending\":true,\"message\":\"Đang kiểm tra cập nhật\"}";
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
  doc["status"]["cpu_freq_mhz"] = getCpuFrequencyMhz();
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
  doc["status"]["cpu_temp_c"] = round(temperatureRead() * 10.0f) / 10.0f;
#else
  doc["status"]["cpu_temp_c"] = 0;
#endif
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
  doc["status"]["psram_active"] = psramFound();
  doc["status"]["psram_capacity_mb"] = psramFound() ? (float)ESP.getPsramSize() / (1024.0f * 1024.0f) : 0;
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
    request->send(409, CONTENT_JSON, "{\"success\":false,\"message\":\"GPIO đang ở chế độ an toàn, audio đã bị vô hiệu hóa\"}");
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
  bool localAudioBusy = (mp3 && mp3->isRunning()) || pcmPlaybackActive || audioOpening;
  return recording || localAudioBusy || otaUpdateActive || fsMaintenanceActive ||
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
  markWebUiActive();
  if (!webUiWorkBusy()) return true;
  sendWebUiUnavailable(request, webUiBusyReason());
  return false;
}

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
  markWebUiActive();
  if (!admitWebUiWork(request)) return;
  static uint32_t lastBuildMs = 0;
  static bool builtOnce = false;
  const uint32_t now = millis();
  if (builtOnce && now - lastBuildMs < 500) {
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

static void mqttSetVolume(int value);

static uint32_t dlnaPlayStartedAtMs = 0;

static void onDlnaPlay(const char *url, const char *title) {
  if (!url || strlen(url) == 0) return;
  UART_LOGF("[DLNA] Bắt đầu phát từ thiết bị cast: %s (%s)", title ? title : "", url);
  dlnaPlayStartedAtMs = millis();
  lockMediaState();
  currentMqttMediaTitle = (title && strlen(title) > 0) ? String(title) : String("DLNA Stream");
  currentMqttMediaCover = "";
  unlockMediaState();
  mqttStateDirty = true;
  stopAudioPlayback(true);
  setLedEffect(LED_SPEAK);
  playMp3Url(url, false, LED_SPEAK);
}

static void onDlnaStop() {
  UART_LOG("[DLNA] Dừng phát từ thiết bị cast.");
  dlnaPlayStartedAtMs = 0;
  stopAudioPlayback(true);
  setLedEffect(micActive() ? LED_OFF : LED_MUTE);
}

static void onDlnaPause() {
  UART_LOG("[DLNA] Tạm dừng phát từ thiết bị cast.");
  dlnaPlayStartedAtMs = 0;
  stopAudioPlayback(true);
  setLedEffect(micActive() ? LED_OFF : LED_MUTE);
}

static void onDlnaVolume(int vol) {
  UART_LOGF("[DLNA] Chỉnh âm lượng từ thiết bị cast: %d%%", vol);
  mqttSetVolume(vol);
}

static void onAirPlayPeak(uint16_t peak) {
  if (peak > currentAudioPeak) {
    currentAudioPeak = peak;
  }
}

static void onAirPlayPlay() {
  UART_LOG("[AirPlay] Bắt đầu phát từ iPhone / Mac.");
  lockMediaState();
  currentMqttMediaTitle = "Apple AirPlay Audio";
  currentMqttMediaCover = "";
  unlockMediaState();
  mqttStateDirty = true;
  stopAudioPlayback(true);
  vbot_bt_slave_suspend();
  uninstallPcmTx();
  suspendMicForAudio();
  setLedEffect(LED_SPEAK);
}

static void onAirPlayStop() {
  UART_LOG("[AirPlay] Dừng phát từ iPhone / Mac.");
  stopAudioPlayback(true);
  resumeMicAfterAudio();
  vTaskDelay(pdMS_TO_TICKS(15));
  installSilentDac();
  vbot_bt_slave_resume();
  setLedEffect(micActive() ? LED_OFF : LED_MUTE);
}

static void onAirPlayVolume(int vol) {
  UART_LOGF("[AirPlay] Chỉnh âm lượng từ iPhone / Mac: %d%%", vol);
  mqttSetVolume(vol);
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
    markWebUiActive();
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
        request->send(400, CONTENT_JSON, "{\"success\":false,\"message\":\"Không nhận được tệp cấu hình\"}");
        return;
      }
      if (upload->tooLarge) {
        free(upload);
        request->send(413, CONTENT_JSON, "{\"success\":false,\"message\":\"Tệp cấu hình vượt quá 8192 byte\"}");
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
  struct HotwordUploadContext {
    File file;
    bool failed = false;
    String err;
    size_t totalBytes = 0;
  };
  web.on(
    "/upload_hotword",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      HotwordUploadContext *ctx = static_cast<HotwordUploadContext *>(request->_tempObject);
      request->_tempObject = nullptr;
      if (!ctx) {
        sendJsonResult(request, 400, false, "Không nhận được tệp tải lên");
        return;
      }
      if (ctx->file) {
        ctx->file.close();
      }
      if (ctx->failed) {
        String msg = ctx->err.length() ? ctx->err : "Tải lên tệp thất bại";
        delete ctx;
        LittleFS.remove("/models/upload_temp.bin");
        sendJsonResult(request, 400, false, msg.c_str());
        return;
      }
      delete ctx;

      String displayName = "";
      if (request->hasParam("name")) {
        displayName = request->getParam("name")->value();
      } else if (request->hasParam("name", true)) {
        displayName = request->getParam("name", true)->value();
      }

      String errOut;
      bool ok = vbot_wakenet_add_custom_model("/models/upload_temp.bin", displayName.c_str(), errOut);
      LittleFS.remove("/models/upload_temp.bin");

      if (!ok) {
        sendJsonResult(request, 400, false, errOut.c_str());
        return;
      }

      sendJsonResult(request, 200, true, "Đã tải lên và nạp hotword tùy chỉnh thành công!");
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if (index == 0) {
        HotwordUploadContext *ctx = new HotwordUploadContext();
        LittleFS.mkdir("/models");
        ctx->file = LittleFS.open("/models/upload_temp.bin", "w");
        if (!ctx->file) {
          ctx->failed = true;
          ctx->err = "Không thể tạo tệp tạm trên LittleFS";
        }
        request->_tempObject = ctx;
        request->onDisconnect([request]() {
          HotwordUploadContext *orphan = static_cast<HotwordUploadContext *>(request->_tempObject);
          if (orphan) {
            request->_tempObject = nullptr;
            if (orphan->file) orphan->file.close();
            delete orphan;
            LittleFS.remove("/models/upload_temp.bin");
          }
        });
      }
      HotwordUploadContext *ctx = static_cast<HotwordUploadContext *>(request->_tempObject);
      if (ctx && !ctx->failed) {
        ctx->totalBytes += len;
        if (ctx->totalBytes > 2000000) {
          ctx->failed = true;
          ctx->err = "Dung lượng tệp vượt quá 2MB";
        } else if (ctx->file.write(data, len) != len) {
          ctx->failed = true;
          ctx->err = "Lỗi ghi dữ liệu vào LittleFS";
        }
      }
      if (final && ctx && ctx->file) {
        ctx->file.close();
      }
    }
  );
  web.on("/delete_hotword", HTTP_POST, [](AsyncWebServerRequest *request) {
    const AsyncWebParameter *pSlot = request->hasParam("slot", true) ? request->getParam("slot", true) : (request->hasParam("slot") ? request->getParam("slot") : nullptr);
    if (!pSlot) {
      sendJsonResult(request, 400, false, "Thiếu tham số slot");
      return;
    }
    int slot = pSlot->value().toInt();
    if (slot < 0 || slot >= WAKENET_MAX_CUSTOM_MODELS) {
      sendJsonResult(request, 400, false, "Slot không hợp lệ (0-4). Các hotword mặc định không được phép xóa!");
      return;
    }
    String errOut;
    bool ok = vbot_wakenet_delete_custom_model((uint8_t)slot, errOut);
    if (!ok) {
      sendJsonResult(request, 400, false, errOut.c_str());
      return;
    }

    lockCfg();
    if (cfg.wakenetModel == (uint8_t)(WAKENET_DEFAULT_MODEL_COUNT + slot)) {
      cfg.wakenetModel = (uint8_t)WAKENET_MODEL_HILILI_TTS;
      lockNvs();
      prefs.putUChar("wn_model", cfg.wakenetModel);
      unlockNvs();
    }
    unlockCfg();

    request->send(200, CONTENT_JSON, "{\"success\":true,\"message\":\"Đã xóa hotword tùy chỉnh thành công!\"}");
  });
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
  web.on("/api/led", HTTP_GET, [](AsyncWebServerRequest *request) {
    sendCorsJsonBody(request, 200, buildManualLedStateJson());
  });
  web.on("/api/led", HTTP_OPTIONS, [](AsyncWebServerRequest *request) {
    sendCorsJsonBody(request, 204, String());
  });
  web.on(
    "/api/led",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      LedApiBody *body = static_cast<LedApiBody *>(request->_tempObject);
      request->_tempObject = nullptr;
      if (!body) {
        sendCorsJsonResult(request, 400, false, "Thiếu JSON body");
        return;
      }
      if (body->tooLarge) {
        delete body;
        sendCorsJsonResult(request, 413, false, "JSON LED vượt quá 512 byte");
        return;
      }
      JsonDocument command;
      DeserializationError parseError = deserializeJson(command, body->data, body->length);
      delete body;
      if (parseError || !command.is<JsonObject>()) {
        sendCorsJsonResult(request, 400, false, "JSON LED không hợp lệ hoặc không phải object");
        return;
      }
      String error;
      if (!applyManualLedCommand(command.as<JsonObjectConst>(), error)) {
        sendCorsJsonResult(request, 409, false, error.c_str());
        return;
      }
      sendCorsJsonBody(request, 200, buildManualLedStateJson());
    },
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        LedApiBody *created = new (std::nothrow) LedApiBody();
        request->_tempObject = created;
        if (created) {
          request->onDisconnect([request]() {
            LedApiBody *orphan = static_cast<LedApiBody *>(request->_tempObject);
            if (orphan) {
              request->_tempObject = nullptr;
              delete orphan;
            }
          });
        }
      }
      LedApiBody *body = static_cast<LedApiBody *>(request->_tempObject);
      if (!body) return;
      if (total > sizeof(body->data) - 1 || index + len > sizeof(body->data) - 1) {
        body->tooLarge = true;
        return;
      }
      memcpy(body->data + index, data, len);
      body->length = index + len;
      body->data[body->length] = '\0';
    }
  );
  web.on("/test_led", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!hardwareGpioValid || !cfg.ledEnabled || !leds) {
      request->send(409, CONTENT_JSON, "{\"success\":false,\"message\":\"LED không sẵn sàng hoặc GPIO đang ở chế độ an toàn\"}");
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
      request->send(409, CONTENT_JSON, "{\"success\":false,\"message\":\"Không thể format LittleFS khi audio đang phát hoặc đang chờ\"}");
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
      sendCorsJsonResult(request, 409, false, "GPIO đang ở chế độ an toàn, audio đã bị vô hiệu hóa");
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
  web.on("/api/bt/cmd", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("action", true)) {
      String action = request->getParam("action", true)->value();
      action.toUpperCase();
      if (action == "PLAY") vbot_bt_slave_send_cmd("CMD:PLAY");
      else if (action == "PAUSE") vbot_bt_slave_send_cmd("CMD:PAUSE");
      else if (action == "NEXT") vbot_bt_slave_send_cmd("CMD:NEXT");
      else if (action == "PREV") vbot_bt_slave_send_cmd("CMD:PREV");
      else if (action == "DISCONNECT") vbot_bt_slave_send_cmd("CMD:DISCONNECT");
      sendCorsJsonResult(request, 200, true, "Đã gửi lệnh điều khiển Bluetooth");
      return;
    }
    sendCorsJsonResult(request, 400, false, "Thiếu tham số action (PLAY, PAUSE, NEXT, PREV, DISCONNECT)");
  });
  web.on("/api/bt/pins", HTTP_GET, [](AsyncWebServerRequest *request) {
    BtSlavePins p = vbot_bt_slave_get_pins();
    BtSlaveInfo info = vbot_bt_slave_get_info();
    JsonDocument doc;
    doc["bclk"] = p.bclk;
    doc["ws"] = p.ws;
    doc["dout"] = p.dout;
    doc["rx"] = p.rx;
    doc["tx"] = p.tx;
    doc["btn1"] = p.btn1;
    doc["btn2"] = p.btn2;
    doc["btn3"] = p.btn3;
    doc["btn4"] = p.btn4;
    doc["loaded"] = p.loaded;
    doc["connected"] = info.connected;
    String res;
    serializeJson(doc, res);
    request->send(200, CONTENT_JSON, res);
  });
  web.on("/api/bt/pins", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("action", true) && request->getParam("action", true)->value() == "reset") {
      vbot_bt_slave_reset_slave_pins();
      sendCorsJsonResult(request, 200, true, "Đã gửi lệnh khôi phục chân mặc định sang ESP32 thường.");
      return;
    }
    if (request->hasParam("action", true) && request->getParam("action", true)->value() == "get") {
      vbot_bt_slave_request_pins();
      sendCorsJsonResult(request, 200, true, "Đã gửi yêu cầu đọc chân sang ESP32 thường.");
      return;
    }
    BtSlavePins p = vbot_bt_slave_get_pins();
    if (request->hasParam("bclk", true)) p.bclk = request->getParam("bclk", true)->value().toInt();
    if (request->hasParam("ws", true))   p.ws   = request->getParam("ws", true)->value().toInt();
    if (request->hasParam("dout", true)) p.dout = request->getParam("dout", true)->value().toInt();
    if (request->hasParam("rx", true))   p.rx   = request->getParam("rx", true)->value().toInt();
    if (request->hasParam("tx", true))   p.tx   = request->getParam("tx", true)->value().toInt();
    if (request->hasParam("btn1", true)) p.btn1 = request->getParam("btn1", true)->value().toInt();
    if (request->hasParam("btn2", true)) p.btn2 = request->getParam("btn2", true)->value().toInt();
    if (request->hasParam("btn3", true)) p.btn3 = request->getParam("btn3", true)->value().toInt();
    if (request->hasParam("btn4", true)) p.btn4 = request->getParam("btn4", true)->value().toInt();
    vbot_bt_slave_set_slave_pins(p);
    sendCorsJsonResult(request, 200, true, "Đã gửi cấu hình GPIO sang ESP32 thường. Chip đang khởi động lại...");
  });
  web.on("/stop_audio", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!stopAudioPlaybackWithFeedback(true)) {
      sendJsonResult(request, 503, false, "Không thể đưa lệnh dừng vào hàng đợi audio");
      return;
    }
    sendJsonResult(request, 200, true, "Đang dừng phát âm thanh");
  });
  web.on("/VBot_Client_Info", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (otaUpdateActive || fsMaintenanceActive) {
      sendWebUiUnavailable(request, webUiBusyReason());
      return;
    }
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
    doc["audio"]["wakenet_enabled"] = cfg.wakenetEnabled;
    doc["audio"]["wakenet_model"] = cfg.wakenetModel;
    doc["audio"]["wakenet_supported"] = vbot_wakenet_is_supported();
  doc["audio"]["aec_supported"] = vbot_aec_is_active();
  doc["audio"]["barge_in_supported"] = vbot_wakenet_is_supported();
    doc["audio"]["psram_active"] = psramFound();
    doc["audio"]["wakenet_model_name"] = vbot_wakenet_get_model_name((WakeNetModelId)cfg.wakenetModel);
    doc["audio"]["hotword_mode"] = cfg.wakenetEnabled ? "wakenet" : (cfg.hotwordStreamEnabled ? "server" : "none");
    {
      JsonArray modelsArr = doc["audio"]["wakenet_models"].to<JsonArray>();
      for (uint8_t m = 0; m < WAKENET_DEFAULT_MODEL_COUNT; m++) {
        JsonObject obj = modelsArr.add<JsonObject>();
        obj["id"] = m;
        obj["name"] = vbot_wakenet_get_model_name((WakeNetModelId)m);
        obj["key"] = vbot_wakenet_get_model_key((WakeNetModelId)m);
        obj["is_custom"] = false;
      }
      for (uint8_t s = 0; s < WAKENET_MAX_CUSTOM_MODELS; s++) {
        WakeNetCustomSlot cslot;
        if (vbot_wakenet_get_custom_slot(s, cslot) && cslot.active) {
          JsonObject obj = modelsArr.add<JsonObject>();
          obj["id"] = WAKENET_DEFAULT_MODEL_COUNT + s;
          obj["name"] = cslot.name;
          obj["key"] = cslot.key;
          obj["is_custom"] = true;
          obj["slot"] = s;
        }
      }
      JsonArray customArr = doc["audio"]["custom_hotwords"].to<JsonArray>();
      for (uint8_t s = 0; s < WAKENET_MAX_CUSTOM_MODELS; s++) {
        WakeNetCustomSlot cslot;
        if (vbot_wakenet_get_custom_slot(s, cslot)) {
          JsonObject obj = customArr.add<JsonObject>();
          obj["slot"] = s;
          obj["active"] = cslot.active;
          if (cslot.active) {
            obj["name"] = cslot.name;
            obj["key"] = cslot.key;
            obj["size"] = cslot.size;
          }
        }
      }
      doc["audio"]["custom_hotwords_count"] = vbot_wakenet_get_custom_count();
      doc["audio"]["custom_hotwords_max"] = WAKENET_MAX_CUSTOM_MODELS;
    }
    doc["audio"]["mic_vad_enabled"] = cfg.micVadEnabled;
    doc["audio"]["mic_vad_gate_open"] = (bool)micVadGateOpen;
    doc["audio"]["mic_noise_floor"] = (int)micVadNoiseFloor;
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
    doc["led"]["error_color"] = cfg.ledErrorColor;
    doc["led"]["error_effect"] = cfg.ledErrorEffect;
    doc["gpio"]["button_mic"] = cfg.pinBtnMic;
    doc["gpio"]["button_volume_up"] = cfg.pinBtnVolUp;
    doc["gpio"]["button_volume_down"] = cfg.pinBtnVolDown;
    doc["gpio"]["button_wakeup"] = cfg.pinBtnWake;
    doc["gpio"]["rotary_clk"] = cfg.pinRotaryClk;
    doc["gpio"]["rotary_dt"] = cfg.pinRotaryDt;
    doc["gpio"]["rotary_sw"] = cfg.pinRotarySw;
    doc["button"]["active"] = cfg.buttonsEnabled;
    doc["button"]["input_mode"] = cfg.inputMode;
    doc["button"]["active_level"] = cfg.buttonActiveLevel;
    doc["button"]["wakeup_long_action"] = cfg.wakeLongAction;
    doc["button"]["wakeup_double_action"] = cfg.wakeDoubleAction;
    doc["button"]["mic_long_action"] = cfg.micLongAction;
    doc["button"]["mic_double_action"] = cfg.micDoubleAction;
    doc["button"]["vol_up_long_action"] = cfg.volUpLongAction;
    doc["button"]["vol_up_double_action"] = cfg.volUpDoubleAction;
    doc["button"]["vol_down_long_action"] = cfg.volDownLongAction;
    doc["button"]["vol_down_double_action"] = cfg.volDownDoubleAction;
    doc["button"]["press_debounce_ms"] = cfg.buttonPressDebounceMs;
    doc["button"]["release_debounce_ms"] = cfg.buttonReleaseDebounceMs;
    doc["button"]["hold_ms"] = cfg.buttonHoldMs;
    doc["button"]["startup_delay_ms"] = cfg.buttonStartupDelayMs;
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
    BtSlaveInfo btInfo = vbot_bt_slave_get_info();
    doc["bluetooth_slave"]["enabled"] = cfg.btSlaveEnabled;
    doc["bluetooth_slave"]["connected"] = btInfo.connected;
    doc["bluetooth_slave"]["status"] = btInfo.status;
    doc["bluetooth_slave"]["title"] = btInfo.title;
    doc["bluetooth_slave"]["artist"] = btInfo.artist;
    doc["bluetooth_slave"]["album"] = btInfo.album;
    doc["bluetooth_slave"]["name"] = btInfo.btName;
    doc["bluetooth_slave"]["peer_device"] = btInfo.peerDevice;
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
    String newButtonActiveLevel = argStr(request, "btnlevel", cfg.buttonActiveLevel);
    bool newButtonActiveHigh = newButtonActiveLevel == "high";
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
      newDacBclk, newDacLrc, newDacDout,
      newButtonActiveHigh
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
    strlcpy(cfg.buttonActiveLevel, newButtonActiveHigh ? "high" : "low", sizeof(cfg.buttonActiveLevel));
    normalizeHexColor(cfg.ledThinkColor, sizeof(cfg.ledThinkColor), argStr(request, "lthink", cfg.ledThinkColor).c_str(), "#0066FF");
    normalizeHexColor(cfg.ledMuteColor, sizeof(cfg.ledMuteColor), argStr(request, "lmute", cfg.ledMuteColor).c_str(), "#FF8C00");
    normalizeHexColor(cfg.ledErrorColor, sizeof(cfg.ledErrorColor), argStr(request, "lerrclr", cfg.ledErrorColor).c_str(), "#FF2814");
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
    cfg.buttonStartupDelayMs = constrain(argInt(request, "bsdelay", cfg.buttonStartupDelayMs), 0, 10000);
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
    if (request->hasParam("wda", true)) {
      strlcpy(cfg.wakeDoubleAction, request->getParam("wda", true)->value().c_str(), sizeof(cfg.wakeDoubleAction));
    }
    if (request->hasParam("mla", true)) {
      strlcpy(cfg.micLongAction, request->getParam("mla", true)->value().c_str(), sizeof(cfg.micLongAction));
    }
    if (request->hasParam("mda", true)) {
      strlcpy(cfg.micDoubleAction, request->getParam("mda", true)->value().c_str(), sizeof(cfg.micDoubleAction));
    }
    if (request->hasParam("vupla", true)) {
      strlcpy(cfg.volUpLongAction, request->getParam("vupla", true)->value().c_str(), sizeof(cfg.volUpLongAction));
    }
    if (request->hasParam("vupda", true)) {
      strlcpy(cfg.volUpDoubleAction, request->getParam("vupda", true)->value().c_str(), sizeof(cfg.volUpDoubleAction));
    }
    if (request->hasParam("vdnla", true)) {
      strlcpy(cfg.volDownLongAction, request->getParam("vdnla", true)->value().c_str(), sizeof(cfg.volDownLongAction));
    }
    if (request->hasParam("vdnda", true)) {
      strlcpy(cfg.volDownDoubleAction, request->getParam("vdnda", true)->value().c_str(), sizeof(cfg.volDownDoubleAction));
    }
    cfg.dacBclk = newDacBclk;
    cfg.dacLrc = newDacLrc;
    cfg.dacDout = newDacDout;
    cfg.btSlaveEnabled = request->hasParam("btsl_en", true);
    cfg.btSlaveBclk = argInt(request, "btsl_bclk", cfg.btSlaveBclk);
    cfg.btSlaveWs = argInt(request, "btsl_ws", cfg.btSlaveWs);
    cfg.btSlaveDin = argInt(request, "btsl_din", cfg.btSlaveDin);
    cfg.btSlaveTx = argInt(request, "btsl_tx", cfg.btSlaveTx);
    cfg.btSlaveRx = argInt(request, "btsl_rx", cfg.btSlaveRx);
    cfg.micEnabled = request->hasParam("micen", true);
    if (request->hasParam("hotword_mode", true)) {
      String mode = request->getParam("hotword_mode", true)->value();
      if (mode == "wakenet") {
        cfg.wakenetEnabled = psramFound() && vbot_wakenet_is_supported();
        cfg.hotwordStreamEnabled = false;
      } else if (mode == "server") {
        cfg.wakenetEnabled = false;
        cfg.hotwordStreamEnabled = true;
      } else { // "none"
        cfg.wakenetEnabled = false;
        cfg.hotwordStreamEnabled = false;
      }
    } else {
      cfg.hotwordStreamEnabled = request->hasParam("hword", true);
      cfg.wakenetEnabled = request->hasParam("wn_en", true) && psramFound() && vbot_wakenet_is_supported();
    }
    if (request->hasParam("wn_model", true)) {
      cfg.wakenetModel = constrain(request->getParam("wn_model", true)->value().toInt(), 0, WAKENET_MODEL_COUNT - 1);
    }
    cfg.micVadEnabled = request->hasParam("mvad", true);
    cfg.conversationMode = request->hasParam("conv", true);
    cfg.startupSoundEnabled = request->hasParam("bootsnd", true);
    cfg.serialLogs = request->hasParam("logs", true);
    cfg.mdnsEnabled = request->hasParam("mdns", true);
    cfg.dlnaEnabled = request->hasParam("dlnaen", true);
    cfg.airplayEnabled = request->hasParam("airplayen", true);
    cfg.nsEnabled = request->hasParam("nsen", true);
    cfg.agcEnabled = request->hasParam("agcen", true);
    if (request->hasParam("agcgain", true)) {
      cfg.agcGain = constrain(request->getParam("agcgain", true)->value().toInt(), 0, 30);
    }
    if (request->hasParam("lspef", true)) {
      cfg.ledSpeakEffect = constrain(request->getParam("lspef", true)->value().toInt(), 0, 3);
    }
    if (request->hasParam("lthkef", true)) {
      cfg.ledThinkEffect = constrain(request->getParam("lthkef", true)->value().toInt(), 0, 2);
    }
    if (request->hasParam("lmuteef", true)) {
      cfg.ledMuteEffect = constrain(request->getParam("lmuteef", true)->value().toInt(), 0, 2);
    }
    if (request->hasParam("lerref", true)) {
      cfg.ledErrorEffect = constrain(request->getParam("lerref", true)->value().toInt(), 0, 3);
    }
    cfg.dfsEnabled = request->hasParam("dfsen", true);
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
    UART_LOG("OTA bắt đầu, đã hủy lịch restart cũ và tạm khóa reset/NVS/LittleFS.");
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
  if (cfg.dlnaEnabled) {
    DlnaCallbacks dlnaCb;
    dlnaCb.onPlay = onDlnaPlay;
    dlnaCb.onStop = onDlnaStop;
    dlnaCb.onPause = onDlnaPause;
    dlnaCb.onVolume = onDlnaVolume;
    vbot_dlna_init(web, cfg.mqttClientName, dlnaCb);
  }
  if (cfg.airplayEnabled) {
    AirPlayCallbacks airplayCb;
    airplayCb.onPlay = onAirPlayPlay;
    airplayCb.onStop = onAirPlayStop;
    airplayCb.onVolume = onAirPlayVolume;
    airplayCb.onPeak = onAirPlayPeak;
    vbot_airplay_set_dac_pins(cfg.dacBclk, cfg.dacLrc, cfg.dacDout);
    vbot_airplay_init(strlen(cfg.clientName) > 0 ? cfg.clientName : cfg.mqttClientName, airplayCb);
  }
  // Khởi tạo nhận âm thanh Bluetooth I2S Slave từ ESP32 thường
  if (cfg.btSlaveEnabled) {
    BtSlaveCallbacks btCb;
    btCb.onPlay = []() {
      UART_LOG("[BT-Slave] Bắt đầu phát nhạc Bluetooth từ ESP32 thường.");
      lockMediaState();
      BtSlaveInfo btInfo = vbot_bt_slave_get_info();
      if (btInfo.title[0]) {
        currentMqttMediaTitle = String(btInfo.title);
        if (btInfo.artist[0]) {
          currentMqttMediaTitle += " - ";
          currentMqttMediaTitle += btInfo.artist;
        }
      } else if (currentMqttMediaTitle.length() == 0) {
        currentMqttMediaTitle = "Bluetooth Audio";
      }
      currentMqttMediaCover = "";
      unlockMediaState();
      mqttStateDirty = true;
      stopAudioPlayback(true);
      suspendMicForAudio();
      // Khong bat LED_SPEAK o day de tranh nhay LED khi moi ket noi hoac khi chua phat nhac
    };
    btCb.onStop = []() {
      UART_LOG("[BT-Slave] Dừng phát nhạc Bluetooth từ ESP32 thường.");
      lockMediaState();
      BtSlaveInfo btInfo = vbot_bt_slave_get_info();
      if (!btInfo.connected) {
        currentMqttMediaTitle = "";
        currentMqttMediaCover = "";
      }
      unlockMediaState();
      mqttStateDirty = true;
      if (!recording && waitingToWake && !wakeSkipRequest) {
        resumeMicAfterAudio();
        setLedEffect(micActive() ? LED_OFF : LED_MUTE);
      }
    };
    btCb.onPeak = [](uint16_t peak) {
      currentAudioPeak = peak;
      // Chi khi co am thanh thuc su (peak > 100) thi moi bat hieu ung LED_SPEAK
      static uint32_t lastBtLedTrigger = 0;
      if (peak > 100 && vbot_bt_slave_is_streaming() && (millis() - lastBtLedTrigger > 2000)) {
        lastBtLedTrigger = millis();
        setLedEffect(LED_SPEAK);
      }
    };
    btCb.onSuspendMic = []() {
      lockMicI2s();
      if (micRxInstalled) {
        i2s_zero_dma_buffer(MIC_I2S_PORT);
        i2s_driver_uninstall(MIC_I2S_PORT);
        micRxInstalled = false;
      }
      unlockMicI2s();
    };
    btCb.onResumeMic = []() {
      initMic();
    };
    btCb.onMeta = [](const char *title, const char *artist) {
      lockMediaState();
      if (title && title[0]) {
        currentMqttMediaTitle = String(title);
        if (artist && artist[0]) {
          currentMqttMediaTitle += " - ";
          currentMqttMediaTitle += artist;
        }
      } else {
        currentMqttMediaTitle = "";
      }
      unlockMediaState();
      mqttStateDirty = true;
    };
    btCb.onStatus = [](const char *status) {
      if (status && strcmp(status, "DISCONNECTED") == 0) {
        lockMediaState();
        currentMqttMediaTitle = "";
        currentMqttMediaCover = "";
        unlockMediaState();
      }
      mqttStateDirty = true;
    };
    btCb.isVBotBusy = isVBotAssistantBusy;
    vbot_bt_slave_set_uart_pins(cfg.btSlaveTx, cfg.btSlaveRx);
    vbot_bt_slave_set_pins(cfg.btSlaveBclk, cfg.btSlaveWs, cfg.btSlaveDin);
    vbot_bt_slave_set_dac_pins(cfg.dacBclk, cfg.dacLrc, cfg.dacDout);
    vbot_bt_slave_set_volume(cfg.volume);
    vbot_bt_slave_sync_config(cfg.clientName, cfg.btSlaveEnabled);
    vbot_bt_slave_init(btCb);
  }
  ElegantOTA.begin(&web);
  web.begin();
}

static void applyVolumeDelta(int delta) {
  lockCfg();
  int next = constrain((int)cfg.volume + delta, 0, 100);
  if (next == cfg.volume) {
    unlockCfg();
    if (cfg.volume > 0 && waitingToWake && !recording && localTutTutAvailable && !vbot_bt_slave_is_streaming()) {
      volumeFeedbackAtMs = millis() + 80;
      volumeFeedbackPending = true;
    }
    return;
  }
  cfg.volume = (uint8_t)next;
  uint8_t volume = cfg.volume;
  unlockCfg();
  vbot_bt_slave_set_volume(volume);
  volumeSaveValue = volume;
  volumeSaveAtMs = millis() + VOLUME_SAVE_DELAY_MS;
  volumeSavePending = true;
  volumeSaveFeedbackPending = false;
  if (!vbot_bt_slave_is_streaming()) {
    volumeFeedbackAtMs = millis() + 140;
    volumeFeedbackPending = true;
  }
  mqttStateDirty = true;
  AudioCommand cmd = {};
  cmd.type = AUDIO_CMD_SET_VOLUME;
  queueAudioCommand(cmd);
  LOGF("Âm lượng: %u", cfg.volume);
  showVolumeOverlay(volume);
}

// Hủy phiên ghi âm từ buttonTask (Core 0) và ủy quyền gửi "stop" sang websocketTask (Core 1).
static void cancelRecordingForButton(bool clearMicQueue = false) {
  bool wasRecording = recording;
  pendingAutoContinue = false;
  recording = false;
  recordingDeadlineMs = 0;
  waitingToWake = true;
  if (wasRecording && wsReady) stopRecordingRequested = true;
  if (clearMicQueue && micQueue) xQueueReset(micQueue);
}

static void micShortPress() {
  lockCfg();
  cfg.micEnabled = !cfg.micEnabled;
  bool enabled = cfg.micEnabled;
  unlockCfg();
  temporaryMicEnabled = false;
  if (!enabled) {
    cancelRecordingForButton(true);
  }
  lockNvs();
  prefs.putBool("micen", enabled);
  unlockNvs();
  mqttStateDirty = true;
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
    cancelRecordingForButton(false);
    restoreTemporaryMic();
    stopAudioPlaybackWithFeedback();
    mqttStateDirty = true;
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
  mqttStateDirty = true;
  LOGF("Chế độ hội thoại: %s", cfg.conversationMode ? "Đang Bật" : "Đang Tắt");
  playMp3Url(conversationMode ? conversationModeOnFeedbackUrl().c_str() : conversationModeOffFeedbackUrl().c_str());
}

static void volUpShortPress() { applyVolumeDelta(5); }
static void volUpLongPress() { applyVolumeDelta(20); }
static void volDownShortPress() { applyVolumeDelta(-5); }
static void volDownLongPress() { applyVolumeDelta(-20); }

static void wakeShortPress() {
  if (!vbotServerStartupEnabled()) return;
  //LOG("Nhấn nút WakeUP / Rotary SW: yêu cầu đánh thức thủ công");
  pendingAutoContinue = false;
  if (!cfg.micEnabled) temporaryMicEnabled = true;
  vbot_bt_slave_suspend();
  stopAudioPlayback(true);
  wakeSkipRequest = true;
}

static bool executeConfiguredButtonAction(const char *actionName, const char *buttonDesc) {
  if (!actionName || !actionName[0] || !strcmp(actionName, "none")) return false;

  if (!strcmp(actionName, "stop_audio")) {
    cancelRecordingForButton(false);
    restoreTemporaryMic();
    stopAudioPlaybackWithFeedback();
    if (vbot_bt_slave_is_streaming()) {
      vbot_bt_slave_send_cmd("CMD:PAUSE");
    }
    mqttStateDirty = true;
    setLedEffect(micActive() ? LED_OFF : LED_MUTE);
    LOGF("%s: Dừng phát âm thanh", buttonDesc ? buttonDesc : "Nút nhấn");
    return true;
  }
  if (!strcmp(actionName, "bt_play_pause")) {
    LOGF("%s: Bật/Tạm dừng Bluetooth", buttonDesc ? buttonDesc : "Nút nhấn");
    if (vbot_bt_slave_is_streaming()) {
      vbot_bt_slave_send_cmd("CMD:PAUSE");
    } else {
      vbot_bt_slave_send_cmd("CMD:PLAY");
    }
    return true;
  }
  if (!strcmp(actionName, "bt_next")) {
    LOGF("%s: Bài tiếp theo Bluetooth", buttonDesc ? buttonDesc : "Nút nhấn");
    vbot_bt_slave_send_cmd("CMD:NEXT");
    return true;
  }
  if (!strcmp(actionName, "bt_prev")) {
    LOGF("%s: Bài trước đó Bluetooth", buttonDesc ? buttonDesc : "Nút nhấn");
    vbot_bt_slave_send_cmd("CMD:PREV");
    return true;
  }
  if (!strcmp(actionName, "toggle_mic")) {
    LOGF("%s: Bật/Tắt Mic", buttonDesc ? buttonDesc : "Nút nhấn");
    micShortPress();
    return true;
  }
  if (otaUpdateActive || fsMaintenanceActive) {
    UART_LOGF("Bỏ qua hành động %s vì OTA/LittleFS đang được cập nhật.", buttonDesc ? buttonDesc : "nút nhấn");
    return true;
  }
  if (!strcmp(actionName, "restart")) {
    LOGF("%s: Khởi động lại ESP", buttonDesc ? buttonDesc : "Nút nhấn");
    scheduleRestart(300, buttonDesc ? buttonDesc : "Lệnh nút nhấn");
    return true;
  }
  if (!strcmp(actionName, "clean_all")) {
    LOGF("%s: Đặt lại dữ liệu cấu hình Config và WiFi", buttonDesc ? buttonDesc : "Nút nhấn");
    beginWifiResetSequence(true);
    return true;
  }
  if (!strcmp(actionName, "reset_wifi")) {
    LOGF("%s: Đặt lại WiFi", buttonDesc ? buttonDesc : "Nút nhấn");
    beginWifiResetSequence(false);
    return true;
  }
  if (!strcmp(actionName, "reset_nvs")) {
    LOGF("%s: Đặt lại cấu hình Config (NVS)", buttonDesc ? buttonDesc : "Nút nhấn");
    if (resetNvsOnly()) scheduleRestart(300, "Reset NVS từ nút nhấn");
    else UART_LOG("Không restart vì reset NVS thất bại.");
    return true;
  }
  return false;
}

static void wakeLongPress() {
  if (executeConfiguredButtonAction(cfg.wakeLongAction, "Nhấn giữ WakeUP / Rotary SW")) return;
  cancelRecordingForButton(false);
  restoreTemporaryMic();
  stopAudioPlaybackWithFeedback();
  mqttStateDirty = true;
  setLedEffect(micActive() ? LED_OFF : LED_MUTE);
  LOG("Phát hiện nhấn giữ nút WakeUP: dừng âm thanh đang phát");
}

static void handleVolumeButton(ButtonRuntime &state, int pin, int delta, const char *longActionName, const char *doubleActionName, const char *btnDesc, bool activeHigh = false) {
  if (pin < 0) return;
  uint32_t now = millis();
  bool raw = (digitalRead(pin) == (activeHigh ? HIGH : LOW));
  if (raw != state.rawPressed) {
    state.rawPressed = raw;
    state.lastRawChangeMs = now;
  }

  bool hasDoubleAction = (doubleActionName && doubleActionName[0] && strcmp(doubleActionName, "none") != 0);
  bool hasLongAction = (longActionName && longActionName[0] && strcmp(longActionName, "none") != 0);

  if (raw != state.stablePressed) {
    uint16_t debounce = raw ? (cfg.buttonPressDebounceMs > 30 ? 30 : cfg.buttonPressDebounceMs)
                            : (cfg.buttonReleaseDebounceMs > 30 ? 30 : cfg.buttonReleaseDebounceMs);
    if (now - state.lastRawChangeMs >= debounce) {
      state.stablePressed = raw;
      if (raw) {
        state.pressStartMs = now;
        state.lastRepeatMs = now;
        state.longFired = false;
        if (!hasDoubleAction) {
          applyVolumeDelta(delta);
        }
      } else {
        if (!state.longFired) {
          if (hasDoubleAction) {
            state.clickCount++;
            state.releaseMs = now;
            if (state.clickCount >= 2) {
              state.clickCount = 0;
              state.releaseMs = 0;
              executeConfiguredButtonAction(doubleActionName, btnDesc);
            }
          }
        } else {
          state.clickCount = 0;
          state.releaseMs = 0;
        }
      }
    }
  }

  if (hasDoubleAction && state.clickCount == 1 && !state.stablePressed) {
    if (now - state.releaseMs >= 280) {
      state.clickCount = 0;
      state.releaseMs = 0;
      applyVolumeDelta(delta);
    }
  }

  if (state.stablePressed && !state.longFired) {
    if (hasLongAction) {
      if (now - state.pressStartMs >= cfg.buttonHoldMs) {
        state.longFired = true;
        state.clickCount = 0;
        state.releaseMs = 0;
        executeConfiguredButtonAction(longActionName, btnDesc);
      }
    } else {
      if (now - state.pressStartMs >= 350) {
        if (now - state.lastRepeatMs >= 120) {
          state.lastRepeatMs = now;
          state.longFired = true;
          applyVolumeDelta(delta);
        }
      }
    }
  }
}

static void handleButton(ButtonRuntime &state, int pin, void (*shortAction)(), const char *longActionName, void (*fallbackLongAction)(), const char *doubleActionName, const char *btnDesc, bool activeHigh = false) {
  if (pin < 0) return;
  uint32_t now = millis();
  bool raw = (digitalRead(pin) == (activeHigh ? HIGH : LOW));
  if (raw != state.rawPressed) {
    state.rawPressed = raw;
    state.lastRawChangeMs = now;
  }

  bool hasDoubleAction = (doubleActionName && doubleActionName[0] && strcmp(doubleActionName, "none") != 0);

  if (raw != state.stablePressed) {
    uint16_t debounce = raw ? cfg.buttonPressDebounceMs : cfg.buttonReleaseDebounceMs;
    if (now - state.lastRawChangeMs >= debounce) {
      state.stablePressed = raw;
      if (raw) {
        state.pressStartMs = now;
        state.longFired = false;
      } else {
        if (!state.longFired) {
          if (hasDoubleAction) {
            state.clickCount++;
            state.releaseMs = now;
            if (state.clickCount >= 2) {
              state.clickCount = 0;
              state.releaseMs = 0;
              executeConfiguredButtonAction(doubleActionName, btnDesc);
            }
          } else {
            state.clickCount = 0;
            state.releaseMs = 0;
            if (shortAction) shortAction();
          }
        } else {
          state.clickCount = 0;
          state.releaseMs = 0;
        }
      }
    }
  }

  if (hasDoubleAction && state.clickCount == 1 && !state.stablePressed) {
    if (now - state.releaseMs >= 280) {
      state.clickCount = 0;
      state.releaseMs = 0;
      if (shortAction) shortAction();
    }
  }

  if (state.stablePressed && !state.longFired && now - state.pressStartMs >= cfg.buttonHoldMs) {
    state.longFired = true;
    state.clickCount = 0;
    state.releaseMs = 0;
    if (longActionName && longActionName[0] && strcmp(longActionName, "none") != 0) {
      executeConfiguredButtonAction(longActionName, btnDesc);
    } else if (fallbackLongAction) {
      fallbackLongAction();
    }
  }
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
  while (millis() < buttonReadyAtMs) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (cfg.buttonStartupDelayMs > 0) {
    LOGF("Nút nhấn đã ổn định và sẵn sàng hoạt động (trễ khởi động %u ms)", cfg.buttonStartupDelayMs);
  }
  for (;;) {
    if (cfg.buttonsEnabled && millis() >= buttonReadyAtMs) {
      if (inputModeRotary()) {
        handleRotary(rotary);
        handleButton(rotarySwButton, cfg.pinRotarySw, wakeShortPress, cfg.wakeLongAction, wakeLongPress, cfg.wakeDoubleAction, "WakeUP / Rotary SW");
      } else {
        rotary.initialized = false;
        bool activeHigh = (strcmp(cfg.buttonActiveLevel, "high") == 0);
        handleButton(micButton, cfg.pinBtnMic, micShortPress, cfg.micLongAction, micLongPress, cfg.micDoubleAction, "Nút Mic", activeHigh);
        handleVolumeButton(upButton, cfg.pinBtnVolUp, 5, cfg.volUpLongAction, cfg.volUpDoubleAction, "Nút Volume+", activeHigh);
        handleVolumeButton(downButton, cfg.pinBtnVolDown, -5, cfg.volDownLongAction, cfg.volDownDoubleAction, "Nút Volume-", activeHigh);
        handleButton(wakeButton, cfg.pinBtnWake, wakeShortPress, cfg.wakeLongAction, wakeLongPress, cfg.wakeDoubleAction, "Nút WakeUP", activeHigh);
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
    uint32_t lastReconnectAttempt = 0;
    while (!WiFi.isConnected() && millis() - start < 180000) {
      if (lastReconnectAttempt == 0 || millis() - lastReconnectAttempt >= 12000) {
        lastReconnectAttempt = millis();
        WiFi.reconnect();
      }
      vTaskDelay(pdMS_TO_TICKS(1000));
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

static bool mqttCanSendWithoutBlocking() {
  if (!mqttClient.connected()) return false;
  int socketFd = mqttNetworkClient.fd();
  if (socketFd < 0) return false;
  fd_set writeSet;
  FD_ZERO(&writeSet);
  FD_SET(socketFd, &writeSet);
  timeval timeout = {0, 0};
  int ready = select(socketFd + 1, nullptr, &writeSet, nullptr, &timeout);
  return ready > 0 && FD_ISSET(socketFd, &writeSet);
}

static void clearMqttStateCache() {
  for (uint8_t i = 0; i < MQTT_STATE_CACHE_SIZE; ++i) {
    mqttStateCache[i].topic = "";
    mqttStateCache[i].payload = "";
  }
}

static void mqttPublish(const char *suffix, const String &value, bool retained = true) {
  if (!mqttClient.connected() || isWebUiActive()) return;
  int freeSlot = -1;
  for (uint8_t i = 0; i < MQTT_STATE_CACHE_SIZE; ++i) {
    if (mqttStateCache[i].topic == suffix) {
      if (!mqttForcePublish && mqttStateCache[i].payload == value) return;
      freeSlot = i;
      break;
    }
    if (freeSlot < 0 && mqttStateCache[i].topic.length() == 0) freeSlot = i;
  }
  mqttClient.loop();
  if (!mqttCanSendWithoutBlocking()) {
    for (int retry = 0; retry < 5; ++retry) {
      vTaskDelay(pdMS_TO_TICKS(10));
      mqttClient.loop();
      if (mqttCanSendWithoutBlocking()) break;
    }
    if (!mqttCanSendWithoutBlocking()) return;
  }
  bool published = mqttClient.publish(mqttTopic(suffix).c_str(), value.c_str(), retained);
  if (published) {
    if (freeSlot >= 0) {
      mqttStateCache[freeSlot].topic = suffix;
      mqttStateCache[freeSlot].payload = value;
    }
    mqttClient.loop();
    vTaskDelay(pdMS_TO_TICKS(mqttForcePublish ? 25 : 10));
  }
}

static void publishMqttState(bool force = false) {
  if (!mqttClient.connected() || isWebUiActive()) return;
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
  mqttPublish("switch/mic_vad/state", cfg.micVadEnabled ? "ON" : "OFF");
  mqttPublish("switch/led_active/state", cfg.ledEnabled ? "ON" : "OFF");
  mqttPublish("switch/bluetooth_active/state", cfg.btSlaveEnabled ? "ON" : "OFF");
  mqttPublish("light/led/state", buildManualLedStateJson());
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
  bool btStreaming = vbot_bt_slave_is_streaming();
  BtSlaveInfo btInfo = vbot_bt_slave_get_info();
  bool btPaused = btInfo.connected && (strcmp(btInfo.status, "PAUSED") == 0 || (!btStreaming && btInfo.title[0] != '\0'));
  bool btActive = btStreaming || btPaused;

  String speakerStatus = recording ? "Đang thu âm" : (!waitingToWake ? "Đang xử lý" : (btStreaming ? "Phát Bluetooth" : "Chờ đánh thức"));
  mqttPublish("sensor/speaker_status/state", speakerStatus);

  if (cfg.btSlaveEnabled) {
    mqttPublish("sensor/bluetooth_enabled/state", "ON");
    mqttPublish("sensor/bluetooth_connection/state", btInfo.connected ? "connected" : "disconnected");
    mqttPublish("sensor/bluetooth_device_name/state", btInfo.peerDevice[0] ? btInfo.peerDevice : (btInfo.connected ? "Đã kết nối" : "Chưa kết nối"));
    mqttPublish("sensor/bluetooth_playing/state", btStreaming ? "playing" : (btPaused ? "paused" : "idle"));
    mqttPublish("sensor/bluetooth_title/state", btInfo.title);
    mqttPublish("sensor/bluetooth_artist/state", btInfo.artist);
    mqttPublish("sensor/bluetooth_album/state", btInfo.album);
    mqttPublish("sensor/bluetooth_audio/state", btStreaming ? "playing" : (btPaused ? "paused" : "idle"));
  } else {
    mqttPublish("sensor/bluetooth_enabled/state", "OFF");
    mqttPublish("sensor/bluetooth_connection/state", "disconnected");
    mqttPublish("sensor/bluetooth_device_name/state", "Chưa kết nối");
    mqttPublish("sensor/bluetooth_playing/state", "idle");
    mqttPublish("sensor/bluetooth_title/state", "");
    mqttPublish("sensor/bluetooth_artist/state", "");
    mqttPublish("sensor/bluetooth_album/state", "");
    mqttPublish("sensor/bluetooth_audio/state", "idle");
  }

  JsonDocument btDoc;
  btDoc["success"] = true;
  btDoc["local_name"] = cfg.clientName[0] ? cfg.clientName : "VBot Client Socket";
  JsonArray devs = btDoc["devices"].to<JsonArray>();
  if (cfg.btSlaveEnabled && btInfo.connected) {
    JsonObject d0 = devs.add<JsonObject>();
    d0["mac"] = "02:00:00:00:00:01";
    d0["name"] = btInfo.peerDevice[0] ? btInfo.peerDevice : "Thiết bị Bluetooth";
    d0["paired"] = true;
    d0["connected"] = true;
    d0["audio_source"] = true;
    d0["audio_sink"] = false;
    d0["output_running"] = false;
    d0["input_running"] = btStreaming;
    d0["output_connected"] = false;
    d0["input_connected"] = true;
  }
  JsonObject roles = btDoc["roles"].to<JsonObject>();
  roles["sink"] = cfg.btSlaveEnabled;
  roles["source"] = false;
  JsonObject route = btDoc["route"].to<JsonObject>();
  route["device"] = "";
  route["name"] = "Loa VBot";
  String btPayload;
  serializeJson(btDoc, btPayload);
  mqttPublish("bluetooth/state", btPayload);

  JsonDocument media;
  bool vbotPlaying = !audioIdle();
  bool playing = vbotPlaying || btStreaming;
  String mediaTitle;
  String mediaCover;
  String mediaAudioUrl;
  String ttsText;
  uint8_t ttsStateValue;
  uint32_t ttsUpdatedAt;
  lockMediaState();
  if (btActive) {
    if (btInfo.title[0]) {
      currentMqttMediaTitle = String(btInfo.title);
      if (btInfo.artist[0]) {
        currentMqttMediaTitle += " - ";
        currentMqttMediaTitle += btInfo.artist;
      }
    } else if (currentMqttMediaTitle.length() == 0) {
      currentMqttMediaTitle = "Bluetooth Audio";
    }
  } else if (!vbotPlaying) {
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

  if (btStreaming) {
    media["state"] = "playing";
  } else if (btPaused) {
    media["state"] = "paused";
  } else if (vbotPlaying) {
    media["state"] = "playing";
  } else {
    media["state"] = "idle";
  }

  media["title"] = btActive
    ? (mediaTitle.length() ? mediaTitle : "Bluetooth Audio")
    : (vbotPlaying ? (mediaTitle.length() ? mediaTitle : "Âm thanh VBot ESP32") : "");
  if (btActive) {
    media["artist"] = btInfo.artist;
    media["album"] = btInfo.album;
    media["connected_device"] = btInfo.peerDevice;
  }
  media["media_url"] = vbotPlaying ? mediaAudioUrl : "";
  media["cover"] = (btActive || vbotPlaying)
      ? (mediaCover.length()
        ? mediaCover
        : String("http://") + cfg.serverHost + "/assets/img/logo.png")
    : "";
  media["source"] = btActive ? "Bluetooth" : (vbotPlaying ? "App VBot" : "Không phát");
  media["source_kind"] = btActive ? "bluetooth" : (vbotPlaying ? "vbot" : "none");
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
  vbot_bt_slave_set_volume(volume);
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
  if (topicValue.endsWith("/script/led_control/set")) {
    JsonDocument command;
    DeserializationError parseError = deserializeJson(command, payload);
    String error;
    if (parseError || !command.is<JsonObject>() || !applyManualLedCommand(command.as<JsonObjectConst>(), error)) {
      logInvalidMqttPayload(topicValue, parseError ? String(parseError.c_str()) : (error.length() ? error : payload));
      return;
    }
  } else if (topicValue.endsWith("/number/volume/set")) {
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
  } else if (topicValue.endsWith("/switch/mic_vad/set")) {
    bool enabled;
    if (!parseMqttSwitch(payload, enabled)) {
      logInvalidMqttPayload(topicValue, payload);
      return;
    }
    lockCfg();
    cfg.micVadEnabled = enabled;
    bool micVadEnabled = cfg.micVadEnabled;
    unlockCfg();
    lockNvs();
    prefs.putBool("mvad", micVadEnabled);
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
  } else if (topicValue.endsWith("/switch/bluetooth_active/set")) {
    bool enabled;
    if (!parseMqttSwitch(payload, enabled)) {
      logInvalidMqttPayload(topicValue, payload);
      return;
    }
    if (enabled != cfg.btSlaveEnabled) {
      lockCfg();
      cfg.btSlaveEnabled = enabled;
      unlockCfg();
      lockNvs();
      prefs.putBool("btsl_en", cfg.btSlaveEnabled);
      unlockNvs();
      vbot_bt_slave_sync_config(cfg.clientName, cfg.btSlaveEnabled);
      mqttStateDirty = true;
    }
  } else if (topicValue.endsWith("/script/media_control/set")) {
    if (payload.equalsIgnoreCase("STOP")) {
      if (vbot_bt_slave_is_streaming()) vbot_bt_slave_send_cmd("PAUSE");
      if (!stopAudioPlayback(true)) LOG("Không thể đưa lệnh MQTT dừng media vào hàng đợi audio.");
      mqttStateDirty = true;
    } else if (payload.equalsIgnoreCase("PAUSE")) {
      if (vbot_bt_slave_is_streaming()) vbot_bt_slave_send_cmd("PAUSE");
      else stopAudioPlayback(false);
      mqttStateDirty = true;
    } else if (payload.equalsIgnoreCase("RESUME") || payload.equalsIgnoreCase("PLAY")) {
      BtSlaveInfo btInfo = vbot_bt_slave_get_info();
      if (btInfo.connected) {
        vbot_bt_slave_send_cmd("PLAY");
      }
      mqttStateDirty = true;
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
  } else if (topicValue.endsWith("/script/bluetooth_control/set")) {
    if (payload.equalsIgnoreCase("PLAY")) vbot_bt_slave_send_cmd("PLAY");
    else if (payload.equalsIgnoreCase("PAUSE")) vbot_bt_slave_send_cmd("PAUSE");
    else if (payload.equalsIgnoreCase("NEXT")) vbot_bt_slave_send_cmd("NEXT");
    else if (payload.equalsIgnoreCase("PREV")) vbot_bt_slave_send_cmd("PREV");
    else if (payload.equalsIgnoreCase("DISCONNECT")) vbot_bt_slave_send_cmd("DISCONNECT");
    else if (payload.equalsIgnoreCase("PAIR")) vbot_bt_slave_send_cmd("STATUS");
    else vbot_bt_slave_send_cmd(payload.c_str());
    mqttStateDirty = true;
  } else if (topicValue.endsWith("/script/bluetooth_output_control/set")) {
    JsonDocument cmdDoc;
    DeserializationError err = deserializeJson(cmdDoc, payload);
    const char *action = !err ? (cmdDoc["action"] | "") : payload.c_str();
    const char *reqId = !err ? (cmdDoc["request_id"] | "") : "";
    if (strcmp(action, "device_disconnect") == 0 || strcmp(action, "disconnect") == 0) {
      vbot_bt_slave_send_cmd("DISCONNECT");
    } else if (strcmp(action, "scan") == 0 || strcmp(action, "pair") == 0) {
      vbot_bt_slave_send_cmd("STATUS");
    }
    JsonDocument resDoc;
    resDoc["success"] = true;
    resDoc["action"] = action;
    if (reqId[0]) resDoc["request_id"] = reqId;
    resDoc["message"] = "Đã thực hiện";
    String resPayload;
    serializeJson(resDoc, resPayload);
    mqttPublish("script/bluetooth_output_control/state", resPayload);
    mqttStateDirty = true;
  } else if (topicValue.endsWith("/script/playlist_control/set")) {
    if (payload.equalsIgnoreCase("next")) {
      BtSlaveInfo btInfo = vbot_bt_slave_get_info();
      if (btInfo.connected) vbot_bt_slave_send_cmd("NEXT");
      mqttStateDirty = true;
    } else if (payload.equalsIgnoreCase("prev")) {
      BtSlaveInfo btInfo = vbot_bt_slave_get_info();
      if (btInfo.connected) vbot_bt_slave_send_cmd("PREV");
      mqttStateDirty = true;
    } else if (payload.equalsIgnoreCase("pause")) {
      if (vbot_bt_slave_is_streaming()) vbot_bt_slave_send_cmd("PAUSE");
      mqttStateDirty = true;
    } else if (payload.equalsIgnoreCase("play") || payload.equalsIgnoreCase("resume")) {
      BtSlaveInfo btInfo = vbot_bt_slave_get_info();
      if (btInfo.connected) vbot_bt_slave_send_cmd("PLAY");
      mqttStateDirty = true;
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
    else if (payload.equalsIgnoreCase("mute")) {
      lockCfg();
      if (cfg.volume > 0) preMuteVolume = cfg.volume;
      unlockCfg();
      mqttSetVolume(0);
    } else if (payload.equalsIgnoreCase("unmute")) {
      mqttSetVolume(preMuteVolume > 0 ? preMuteVolume : 50);
    } else {
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
  mix(cfg.micVadEnabled);
  mix(cfg.ledEnabled);
  mix(cfg.btSlaveEnabled);
  ManualLedState manual = manualLedSnapshot();
  mix(manual.on);
  mix(manual.red);
  mix(manual.green);
  mix(manual.blue);
  mix(manual.brightness);
  mix(manual.effect);
  mix(WiFi.isConnected());
  mix(WiFi.isConnected() ? (uint32_t)WiFi.localIP() : 0);
  mix(wsReady);
  mix(recording);
  mix(waitingToWake);
  mix(mdnsStarted);
  mix(!audioIdle());
  BtSlaveInfo btInfo = vbot_bt_slave_get_info();
  mix(btInfo.connected);
  mix(vbot_bt_slave_is_streaming());
  auto hashStr = [&mix](const char *s) {
    if (!s) return;
    while (*s) mix((uint8_t)*s++);
  };
  hashStr(btInfo.title);
  hashStr(btInfo.status);
  hashStr(btInfo.peerDevice);
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
  if (cfg.airplayEnabled) {
    vbot_airplay_register_mdns();
  }
}

static void mqttSubscribePaced(const char *suffix) {
  mqttClient.loop();
  if (mqttCanSendWithoutBlocking()) {
    mqttClient.subscribe(mqttTopic(suffix).c_str(), 1);
  }
  mqttClient.loop();
  vTaskDelay(pdMS_TO_TICKS(25));
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
  if (isWebUiActive()) return;
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
    mqttSubscribePaced("script/led_control/set");
    mqttSubscribePaced("switch/+/set");
    mqttSubscribePaced("script/media_control/set");
    mqttSubscribePaced("script/bluetooth_control/set");
    mqttSubscribePaced("script/bluetooth_output_control/set");
    mqttSubscribePaced("script/playlist_control/set");
    mqttSubscribePaced("script/vbot_tts/set");
    mqttSubscribePaced("script/volume_control/set");
    mqttSubscribePaced("script/button_control/set");
    mqttSubscribePaced("script/power_action/set");
    mqttSubscribePaced("script/state_sync/set");
    clearMqttStateCache();
    vTaskDelay(pdMS_TO_TICKS(50));
    mqttClient.loop();
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
  if (!nvsMutex) fatalHalt("Không tạo được NVS mutex Semaphore");
  micI2sMutex = xSemaphoreCreateMutex();
  if (!micI2sMutex) fatalHalt("Không tạo được microphone I2S mutex Semaphore");
  updateMutex = xSemaphoreCreateMutex();
  if (!updateMutex) fatalHalt("Không tạo được update mutex Semaphore");
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
    cfg.dacBclk, cfg.dacLrc, cfg.dacDout,
    strcmp(cfg.buttonActiveLevel, "high") == 0
  );
  hardwareGpioValid = hardwareGpioErrors.length() == 0;
  if (!hardwareGpioValid) {
    Serial.println("Cấu hình GPIO không hợp lệ. Khởi động chế độ an toàn WebUI, quá trình khởi tạo GPIO phần cứng bị vô hiệu hóa:");
    Serial.println(hardwareGpioErrors);
  }
  UART_LOG("Booting - Khởi động chương trình VBot Client ESP32");
  UART_LOGF("[BOOT] Bluetooth I2S & UART: %s (BCLK=GPIO%d, WS=GPIO%d, DIN=GPIO%d, TX=GPIO%d, RX=GPIO%d)",
            cfg.btSlaveEnabled ? "BAT (Enabled)" : "TAT (Disabled)",
            cfg.btSlaveBclk, cfg.btSlaveWs, cfg.btSlaveDin, cfg.btSlaveTx, cfg.btSlaveRx);

  if (hardwareGpioValid) initLed();

  WiFi.mode(WIFI_STA);
  // Truyền mic và audio thời gian thực cần độ trễ ổn định hơn chế độ tiết kiệm điện WiFi.
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  String apName = configPortalSsid();
  if (!wm.autoConnect(apName.c_str())) {
    LOG("Lỗi hết thời gian chờ kết nối WiFi, tiếp tục thực hiện tác vụ thử lại kết nối.");
  }
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);

  if (psramFound() && !mp3PsramStreamBuffer) {
    mp3PsramStreamBuffer = reinterpret_cast<uint8_t *>(ps_malloc(MP3_STREAM_BUFFER_SIZE_PSRAM));
    if (mp3PsramStreamBuffer) {
      LOGF("Đã cấp phát MP3 Stream Buffer %u bytes trên PSRAM.", (unsigned)MP3_STREAM_BUFFER_SIZE_PSRAM);
    }
  }

  if (!mp3PreallocBuffer) {
    mp3PreallocBuffer = reinterpret_cast<uint8_t *>(
      psramFound() ? ps_malloc(MP3_PREALLOC_SIZE) : malloc(MP3_PREALLOC_SIZE)
    );
    if (!mp3PreallocBuffer && psramFound()) {
      mp3PreallocBuffer = reinterpret_cast<uint8_t *>(malloc(MP3_PREALLOC_SIZE));
    }
    if (mp3PreallocBuffer) {
      LOGF("Đã cấp phát bộ đệm giải mã MP3 cố định: %u byte (%s)",
           (unsigned)MP3_PREALLOC_SIZE,
           psramFound() ? "PSRAM" : "DRAM");
    } else {
      LOG("Cảnh báo: Không thể cấp phát bộ đệm MP3 cố định, dùng cấp phát động.");
    }
  }

  const size_t pcmPoolTotalBytes = DAC_QUEUE_LEN * PCM_POOL_BUFFER_SIZE;
  pcmPool = reinterpret_cast<uint8_t (*)[PCM_POOL_BUFFER_SIZE]>(
    psramFound() ? ps_malloc(pcmPoolTotalBytes) : malloc(pcmPoolTotalBytes)
  );
  if (!pcmPool) pcmPool = reinterpret_cast<uint8_t (*)[PCM_POOL_BUFFER_SIZE]>(malloc(pcmPoolTotalBytes));
  if (!pcmPool) fatalHalt("Không cấp phát được bộ nhớ cho pcmPool.");

  const size_t vadPreRollBytes = VAD_PREROLL_FRAMES * sizeof(MicFrame);
  vadPreRoll = reinterpret_cast<MicFrame *>(
    psramFound() ? ps_malloc(vadPreRollBytes) : malloc(vadPreRollBytes)
  );
  if (!vadPreRoll) vadPreRoll = reinterpret_cast<MicFrame *>(malloc(vadPreRollBytes));

  micQueue = xQueueCreate(WS_SEND_QUEUE_LEN, sizeof(MicFrame));
  audioQueue = xQueueCreate(DAC_QUEUE_LEN, sizeof(AudioCommand));
  pcmFreeQueue = xQueueCreate(DAC_QUEUE_LEN, sizeof(uint8_t));
  if (!micQueue) fatalHalt("Không tạo được micQueue.");
  if (!audioQueue) fatalHalt("Không tạo được audioQueue.");
  if (!pcmFreeQueue) fatalHalt("Không tạo được pcmFreeQueue.");
  for (uint8_t slot = 0; slot < DAC_QUEUE_LEN; ++slot) {
    xQueueSend(pcmFreeQueue, &slot, 0);
  }
  if (!LittleFS.begin(true, "/littlefs", 4, "littlefs")) {
    LOG("LittleFS mount lỗi, WebUI sẽ không hoạt động cho tới khi upload file: littlefs.bin");
  }
  if (LittleFS.totalBytes() > 0) refreshLocalSoundCache();
  vbot_wakenet_init_custom_storage();

  if (hardwareGpioValid) {
    initButtons();
    if (vbotServerStartupEnabled()) initMic();
    if (cfg.wakenetEnabled) {
      vbot_wakenet_init((WakeNetModelId)cfg.wakenetModel);
    }
    initAudioMp3();
    installSilentDac();
  }
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
  LOGF("Khởi động hoàn tất: free heap=%u byte, largest block=%u byte", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}

static void maintainDfs() {
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
  if (!cfg.dfsEnabled) {
    if (getCpuFrequencyMhz() != 240) {
      setCpuFrequencyMhz(240);
    }
    return;
  }
  static uint32_t lastDfsMs = 0;
  static uint32_t lastBusyMs = 0;
  uint32_t now = millis();
  if (now - lastDfsMs < 1000) return;
  lastDfsMs = now;

  // Nếu đang phát âm thanh, đang ghi âm, đang nhận diện WakeNet, hoặc microphone hoạt động -> chạy 240MHz
  bool isBusy = (!audioIdle() || recording || !waitingToWake || cfg.wakenetEnabled);
  if (isBusy) {
    lastBusyMs = now;
    if (getCpuFrequencyMhz() != 240) {
      setCpuFrequencyMhz(240);
      LOGF("[DFS] Chuyển xung nhịp CPU: 240 MHz (Trạng thái: Hoạt động / WakeNet / Âm thanh)");
    }
    return;
  }

  // Chỉ hạ xung xuống 160MHz khi thiết bị hoàn toàn rảnh rỗi trong ít nhất 15 giây liên tục
  if (now - lastBusyMs > 15000) {
    if (getCpuFrequencyMhz() != 160) {
      setCpuFrequencyMhz(160);
      LOGF("[DFS] Chuyển xung nhịp CPU: 160 MHz (Trạng thái: Nghỉ ngơi làm mát chip)");
    }
  }
#endif
}

void loop() {
  ElegantOTA.loop();
  maintainMdns();
  maintainMqtt();
  static uint32_t lastDlnaSyncMs = 0;
  if (cfg.dlnaEnabled && vbot_dlna_is_running() && millis() - lastDlnaSyncMs >= 500) {
    lastDlnaSyncMs = millis();
    bool dlnaWarmup = (dlnaPlayStartedAtMs > 0 && millis() - dlnaPlayStartedAtMs < 5000);
    if (!dlnaWarmup) {
      vbot_dlna_set_state(audioIdle() ? "STOPPED" : "PLAYING");
    }
    vbot_dlna_set_volume(cfg.volume);
  }
  runPendingResetSequences();
  maintainDfs();
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
