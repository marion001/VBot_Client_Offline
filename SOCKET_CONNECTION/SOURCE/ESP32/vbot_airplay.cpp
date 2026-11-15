//Code By: Vũ Tuyển
//Designed by: BootstrapMade
//GitHub VBot: https://github.com/marion001/VBot_Offline.git
//Facebook Group: https://www.facebook.com/groups/1148385343358824
//Facebook: https://www.facebook.com/TWFyaW9uMDAx
//Email: VBot.Assistant@gmail.com

#include "vbot_airplay.h"
#include "airplay_rsa_key.h"
#include <WiFi.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <mdns.h>
#include <driver/i2s.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/aes.h>
#include <mbedtls/base64.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

extern "C" {
  #include "alac.h"
}

// Bật (1) nếu cần debug chi tiết từng gói tin và thống kê chu kỳ; Tắt (0) ở bản release để giải phóng CPU/RAM/UART
#define AIRPLAY_DEBUG_VERBOSE 0

static bool airplayRunning = false;
static volatile bool airplayStreaming = false;
static volatile bool s_airplaySuspended = false;
static AirPlayCallbacks airplayCallbacks;
static String airplayFriendlyName = "VBot Speaker";
static volatile int airplayCurrentVolume = 60;
static TaskHandle_t rtspTaskHandle = nullptr;
static TaskHandle_t audioUdpTaskHandle = nullptr;
static TaskHandle_t audioDecodeTaskHandle = nullptr;
static TaskHandle_t audioPlayTaskHandle = nullptr;

static constexpr int AIRPLAY_PORT = 7000;
static constexpr int RTP_AUDIO_PORT = 6000;
static constexpr int RTP_CONTROL_PORT = 6001;
static constexpr int RTP_TIMING_PORT = 6002;

static struct in_addr s_clientIp = {};
static volatile uint16_t s_clientControlPort = 0;
static volatile uint16_t s_clientTimingPort = 0;
static TaskHandle_t audioControlTaskHandle = NULL;
static volatile bool s_isDecoding = false;
static volatile bool s_needRebuffer = true;

struct AirPlayTimingPacket {
  uint8_t leader;
  uint8_t type;
  uint16_t seqno;
  uint32_t filler;
  uint64_t origin;
  uint64_t receive;
  uint64_t transmit;
} __attribute__((packed));

// Cấu hình chân phần cứng I2S DAC
static int8_t airplayDacBclk = -1;
static int8_t airplayDacLrc = -1;
static int8_t airplayDacDout = -1;
static volatile bool airplayI2sInstalled = false;

// ALAC Decoder & AES Decryption
static alac_file *alacDecoder = nullptr;
static uint8_t airplayAesKey[16];
static uint8_t airplayAesIv[16];
static bool hasAesKey = false;
static mbedtls_aes_context aesDecCtx;
static bool aesCtxInited = false;

static mbedtls_pk_context s_airplay_pk;
static bool s_airplay_pk_ready = false;

// Jitter Audio RingBuffer 512KB trên PSRAM (~3.0 giây audio đệm)
constexpr size_t AIRPLAY_RINGBUF_SIZE = 512 * 1024;
// RAOP is a realtime UDP stream. Keep roughly two seconds locally so Wi-Fi
// scheduling bursts do not reach I2S as silence or chopped audio.
constexpr size_t AIRPLAY_INITIAL_BUFFER = 264 * 1024;  // ~1.5s at 44.1kHz stereo S16 (an toan cho chu ky DTIM cua iOS)
constexpr size_t AIRPLAY_LOW_WATERMARK = 4 * 1024;     // ~23ms reserve (chi ngung khi buffer thuc su can)
constexpr size_t AIRPLAY_RECOVERY_BUFFER = 32 * 1024;  // ~180ms hysteresis (khoi phuc nhanh, khong im lang)

static volatile uint32_t s_rtpPacketsReceived = 0;
static volatile uint32_t s_rtpPacketsDropped = 0;
static volatile uint32_t s_alacFramesDecoded = 0;
static volatile uint32_t s_pcmBytesDecoded = 0;
static volatile uint32_t s_pcmConcealedFrames = 0;

struct AudioRingBuffer {
  uint8_t *buffer = nullptr;
  size_t capacity = 0;
  volatile size_t head = 0;
  volatile size_t tail = 0;
  SemaphoreHandle_t mutex = nullptr;

  bool init(size_t size) {
    if (!mutex) mutex = xSemaphoreCreateMutex();
    if (!mutex) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (buffer) free(buffer);
    capacity = size;
    head = tail = 0;
    if (psramFound()) {
      buffer = (uint8_t*)ps_malloc(capacity);
    }
    if (!buffer) {
      buffer = (uint8_t*)malloc(capacity);
    }
    bool ok = buffer != nullptr;
    xSemaphoreGive(mutex);
    return ok;
  }

  void freeBuffer() {
    if (mutex) xSemaphoreTake(mutex, portMAX_DELAY);
    if (buffer) {
      free(buffer);
      buffer = nullptr;
    }
    head = tail = 0;
    if (mutex) xSemaphoreGive(mutex);
  }

  void reset() {
    if (!mutex) return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    head = tail = 0;
    xSemaphoreGive(mutex);
  }

  size_t available() {
    if (!mutex) return 0;
    xSemaphoreTake(mutex, portMAX_DELAY);
    size_t h = head;
    size_t t = tail;
    size_t result = h >= t ? h - t : capacity - (t - h);
    xSemaphoreGive(mutex);
    return result;
  }

  size_t freeSpace() {
    if (!mutex) return 0;
    xSemaphoreTake(mutex, portMAX_DELAY);
    size_t h = head;
    size_t t = tail;
    size_t used = h >= t ? h - t : capacity - (t - h);
    size_t result = capacity > 0 ? capacity - 1 - used : 0;
    xSemaphoreGive(mutex);
    return result;
  }

  size_t write(const uint8_t *data, size_t len) {
    if (!mutex || len == 0) return 0;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (!buffer || capacity == 0) {
      xSemaphoreGive(mutex);
      return 0;
    }
    size_t h = head;
    size_t t = tail;
    size_t used = h >= t ? h - t : capacity - (t - h);
    size_t space = capacity - 1 - used;
    if (len > space) len = space;
    if (len == 0) {
      xSemaphoreGive(mutex);
      return 0;
    }
    size_t firstPart = (h + len <= capacity) ? len : (capacity - h);
    memcpy(buffer + h, data, firstPart);
    if (len > firstPart) {
      memcpy(buffer, data + firstPart, len - firstPart);
    }
    head = (h + len) % capacity;
    xSemaphoreGive(mutex);
    return len;
  }

  size_t read(uint8_t *out, size_t len) {
    if (!mutex || len == 0) return 0;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (!buffer || capacity == 0) {
      xSemaphoreGive(mutex);
      return 0;
    }
    size_t h = head;
    size_t t = tail;
    size_t avail = h >= t ? h - t : capacity - (t - h);
    if (len > avail) len = avail;
    if (len == 0) {
      xSemaphoreGive(mutex);
      return 0;
    }
    size_t firstPart = (t + len <= capacity) ? len : (capacity - t);
    memcpy(out, buffer + t, firstPart);
    if (len > firstPart) {
      memcpy(out + firstPart, buffer, len - firstPart);
    }
    tail = (t + len) % capacity;
    xSemaphoreGive(mutex);
    return len;
  }
};

static AudioRingBuffer s_audioRingBuf;

struct RawAudioPacket {
  uint16_t len;
  uint16_t seq;
  uint32_t timestamp;
  bool ready;
  uint8_t data[1500];
};

struct RawPacketRing {
  RawAudioPacket *packets = nullptr;
  size_t capacity = 512; // Cửa sổ 512 gói UDP raw (~4 giây), hấp thụ burst/retransmit trễ.
  size_t activePackets = 0;
  volatile size_t head = 0;
  volatile size_t tail = 0;
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  bool sequenceStarted = false;
  bool highestSequenceStarted = false;
  uint16_t expectedSeq = 0;
  uint16_t highestSeq = 0;
  uint32_t gapStartedMs = 0;
  uint16_t pendingResendFirst = 0;
  uint16_t pendingResendCount = 0;
  bool pendingResend = false;
  uint16_t lastSentReqFirst = 0;
  uint16_t lastSentReqCount = 0;
  uint32_t lastSentReqMs = 0;
  uint16_t pendingConcealmentFrames = 0;
  uint8_t resendAttempts = 0;
  uint32_t lastResendRequestMs = 0;
  uint32_t skippedPackets = 0;
  uint32_t reorderedPackets = 0;
  uint32_t resendRequests = 0;
  uint32_t retransmittedPackets = 0;
  uint32_t sequenceResyncs = 0;

  static constexpr uint8_t MAX_RESEND_ATTEMPTS = 3;

  bool init(size_t cap = 256) {
    capacity = cap;
    head = tail = 0;
    activePackets = 0;
    if (packets) free(packets);
    if (psramFound()) {
      packets = (RawAudioPacket*)ps_malloc(capacity * sizeof(RawAudioPacket));
    }
    if (!packets) {
      packets = (RawAudioPacket*)malloc(capacity * sizeof(RawAudioPacket));
    }
    if (!packets) return false;
    memset(packets, 0, capacity * sizeof(RawAudioPacket));
    sequenceStarted = false;
    highestSequenceStarted = false;
    pendingResend = false;
    lastSentReqFirst = 0;
    lastSentReqCount = 0;
    lastSentReqMs = 0;
    pendingConcealmentFrames = 0;
    resendAttempts = 0;
    lastResendRequestMs = 0;
    skippedPackets = reorderedPackets = resendRequests = retransmittedPackets = sequenceResyncs = 0;
    return true;
  }

  void freeBuffer() {
    if (packets) {
      free(packets);
      packets = nullptr;
    }
    head = tail = 0;
    activePackets = 0;
    sequenceStarted = false;
    highestSequenceStarted = false;
    pendingResend = false;
    lastSentReqFirst = 0;
    lastSentReqCount = 0;
    lastSentReqMs = 0;
    pendingConcealmentFrames = 0;
    resendAttempts = 0;
    lastResendRequestMs = 0;
  }

  void reset() {
    portENTER_CRITICAL(&mux);
    head = tail = 0;
    activePackets = 0;
    sequenceStarted = false;
    highestSequenceStarted = false;
    gapStartedMs = 0;
    pendingResend = false;
    lastSentReqFirst = 0;
    lastSentReqCount = 0;
    lastSentReqMs = 0;
    pendingConcealmentFrames = 0;
    resendAttempts = 0;
    lastResendRequestMs = 0;
    skippedPackets = 0;
    reorderedPackets = 0;
    resendRequests = 0;
    retransmittedPackets = 0;
    sequenceResyncs = 0;
    if (packets) {
      for (size_t i = 0; i < capacity; ++i) packets[i].ready = false;
    }
    portEXIT_CRITICAL(&mux);
  }

  size_t count() {
    portENTER_CRITICAL(&mux);
    size_t readyCount = activePackets;
    portEXIT_CRITICAL(&mux);
    return readyCount;
  }

  bool push(const uint8_t *data, size_t len, bool retransmitted = false) {
    if (!packets || len < 12 || len > 1500) return false;
    // Audio RTP payload type 0x60. Sync packets are handled by the control path.
    if ((data[1] & 0x7f) != 0x60) return false;
    uint16_t seq = ((uint16_t)data[2] << 8) | data[3];
    uint32_t timestamp = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) |
                         ((uint32_t)data[6] << 8) | data[7];
    portENTER_CRITICAL(&mux);
    if (!sequenceStarted) {
      expectedSeq = seq;
      sequenceStarted = true;
    }
    int16_t distance = (int16_t)(seq - expectedSeq);
    if (distance >= (int16_t)capacity) {
      // The sender advanced beyond the receive window while we were stalled.
      for (size_t i = 0; i < capacity; ++i) packets[i].ready = false;
      activePackets = 0;
      expectedSeq = seq;
      highestSeq = seq;
      highestSequenceStarted = true;
      gapStartedMs = 0;
      pendingResend = false;
      pendingConcealmentFrames = 0;
      resendAttempts = 0;
      lastResendRequestMs = 0;
      sequenceResyncs++;
      distance = 0;
    }
    if (distance < 0) {
      portEXIT_CRITICAL(&mux);
      return false; // Duplicate or packet arrived after its playback deadline.
    }
    if (!highestSequenceStarted) {
      highestSeq = seq;
      highestSequenceStarted = true;
    } else {
      int16_t fromHighest = (int16_t)(seq - highestSeq);
      if (fromHighest > 0) {
        highestSeq = seq;
      } else if (fromHighest < 0 && distance > 0) {
        reorderedPackets++;
      }
    }
    size_t slot = seq % capacity;
    if (packets[slot].ready) {
      if (packets[slot].seq == seq) {
        portEXIT_CRITICAL(&mux);
        return false;
      }
      if (activePackets > 0) activePackets--;
    }
    packets[slot].len = (uint16_t)len;
    packets[slot].seq = seq;
    packets[slot].timestamp = timestamp;
    memcpy(packets[slot].data, data, len);
    packets[slot].ready = true;
    activePackets++;
    if (retransmitted) retransmittedPackets++;
    portEXIT_CRITICAL(&mux);
    return true;
  }

  bool pop(RawAudioPacket *out, size_t audioBufAvail = 0) {
    if (!packets || !out) return false;
    portENTER_CRITICAL(&mux);
    if (!sequenceStarted) {
      portEXIT_CRITICAL(&mux);
      return false;
    }
    size_t slot = expectedSeq % capacity;
    if (packets[slot].ready && packets[slot].seq == expectedSeq) {
      *out = packets[slot];
      packets[slot].ready = false;
      if (activePackets > 0) activePackets--;
      expectedSeq++;

      // Kiểm tra nếu gói kế tiếp đã có sẵn thì gap đã được giải quyết xong hoàn toàn
      size_t nextSlot = expectedSeq % capacity;
      if (packets[nextSlot].ready && packets[nextSlot].seq == expectedSeq) {
        gapStartedMs = 0;
        resendAttempts = 0;
      }
      // Nếu gói kế tiếp vẫn chưa có sẵn, ta giữ nguyên gapStartedMs và resendAttempts
      // để tiếp tục theo dõi tiến độ retransmit mà không bị lặp gửi trùng lặp dồn dập
      portEXIT_CRITICAL(&mux);
      return true;
    }

    int16_t nearestDistance = INT16_MAX;
    size_t readyCount = activePackets;

    // Quét nhanh các slot ngay phía trước (tối đa 64 slot) để không lặp vô ích qua PSRAM
    for (uint16_t d = 1; d <= 64; ++d) {
      size_t s = (expectedSeq + d) % capacity;
      if (packets[s].ready) {
        int16_t dist = (int16_t)(packets[s].seq - expectedSeq);
        if (dist > 0) {
          nearestDistance = dist;
          break;
        }
      }
    }
    if (nearestDistance == INT16_MAX && readyCount > 0) {
      for (size_t i = 0; i < capacity; ++i) {
        if (!packets[i].ready) continue;
        int16_t dist = (int16_t)(packets[i].seq - expectedSeq);
        if (dist > 0 && dist < nearestDistance) nearestDistance = dist;
      }
    }
    if (nearestDistance == INT16_MAX) {
      gapStartedMs = 0;
      pendingResend = false;
      resendAttempts = 0;
      lastResendRequestMs = 0;
      lastSentReqFirst = 0;
      lastSentReqCount = 0;
      lastSentReqMs = 0;
      portEXIT_CRITICAL(&mux);
      return false;
    }

    uint32_t now = millis();
    if (gapStartedMs == 0) {
      gapStartedMs = now ? now : 1;
      resendAttempts = 0;
      lastResendRequestMs = 0;
    }
    uint32_t gapAge = (uint32_t)(now - gapStartedMs);

    // Timeout thông minh tương thích với cơ chế DTIM / Power-Save của iPhone khi khóa màn hình:
    // Khi khóa màn hình, chip Wi-Fi iOS sleep theo chu kỳ beacon DTIM (khoảng 100-200ms) rồi mới phát burst.
    // Khi đệm PCM dồi dào, ta kiên nhẫn cho iPhone 250-280ms để nhận gói retransmit mà không làm rớt gói.
    uint32_t initialDelayMs = 12;
    uint32_t retryIntervalMs = 50;
    uint32_t maxWaitMs = 220;

    if (audioBufAvail >= 150 * 1024) {
      initialDelayMs = 15;
      retryIntervalMs = 50;
      maxWaitMs = 280;
    } else if (audioBufAvail >= 70 * 1024) {
      initialDelayMs = 10;
      retryIntervalMs = 40;
      maxWaitMs = 180;
    } else if (audioBufAvail >= 24 * 1024) {
      initialDelayMs = 6;
      retryIntervalMs = 25;
      maxWaitMs = 60;
    } else {
      initialDelayMs = 4;
      retryIntervalMs = 15;
      maxWaitMs = 25;
    }

    // Chỉ hạ maxWaitMs nếu hàng đợi RawPacketRing thực sự sắp đầy (gần chạm 512 slot capacity)
    // để tránh tràn buffer UDP:
    if (readyCount >= 420 && maxWaitMs > 50) {
      maxWaitMs = 50;
    } else if (readyCount >= 340 && maxWaitMs > 100) {
      maxWaitMs = 100;
    }

    // Kiểm tra xem khoảng gói bị thiếu [expectedSeq .. expectedSeq + nearestDistance)
    // đã được gửi yêu cầu retransmit gần đây hay chưa (tránh spam bão request làm nghẽn Wi-Fi)
    int16_t coveredDist = (int16_t)(expectedSeq - lastSentReqFirst);
    bool alreadyCovered = (lastSentReqMs > 0) &&
                          (coveredDist >= 0) &&
                          (coveredDist < (int16_t)lastSentReqCount) &&
                          ((uint32_t)(now - lastSentReqMs) < retryIntervalMs);

    bool notifyControl = false;
    if (!pendingResend && !alreadyCovered && resendAttempts < MAX_RESEND_ATTEMPTS) {
      if (resendAttempts == 0 && gapAge >= initialDelayMs) {
        pendingResendFirst = expectedSeq;
        pendingResendCount = (uint16_t)nearestDistance;
        pendingResend = true;
        lastSentReqFirst = expectedSeq;
        lastSentReqCount = (uint16_t)nearestDistance;
        lastSentReqMs = now;
        resendAttempts++;
        resendRequests++;
        lastResendRequestMs = now;
        notifyControl = true;
      } else if (resendAttempts > 0 && (uint32_t)(now - lastResendRequestMs) >= retryIntervalMs) {
        pendingResendFirst = expectedSeq;
        pendingResendCount = (uint16_t)nearestDistance;
        pendingResend = true;
        lastSentReqFirst = expectedSeq;
        lastSentReqCount = (uint16_t)nearestDistance;
        lastSentReqMs = now;
        resendAttempts++;
        resendRequests++;
        lastResendRequestMs = now;
        notifyControl = true;
      }
    }
    if (gapAge < maxWaitMs) {
      portEXIT_CRITICAL(&mux);
      if (notifyControl && audioControlTaskHandle) {
        xTaskNotifyGive(audioControlTaskHandle);
      }
      return false;
    }

    // Quá thời gian chờ: Bỏ qua gói bị mất, chèn frame fade/silence để giữ nhạc trôi chảy
    uint16_t missingCount = (uint16_t)nearestDistance;
    expectedSeq = (uint16_t)(expectedSeq + missingCount);
    skippedPackets += missingCount;
    uint32_t concealment = (uint32_t)pendingConcealmentFrames + missingCount;
    pendingConcealmentFrames = concealment > UINT16_MAX ? UINT16_MAX : (uint16_t)concealment;
    pendingResend = false;
    gapStartedMs = 0;
    resendAttempts = 0;
    lastResendRequestMs = 0;
    lastSentReqFirst = 0;
    lastSentReqCount = 0;
    lastSentReqMs = 0;
    portEXIT_CRITICAL(&mux);
    if (notifyControl && audioControlTaskHandle) {
      xTaskNotifyGive(audioControlTaskHandle);
    }
    return false;
  }

  bool takeConcealmentFrame() {
    portENTER_CRITICAL(&mux);
    if (pendingConcealmentFrames == 0) {
      portEXIT_CRITICAL(&mux);
      return false;
    }
    pendingConcealmentFrames--;
    portEXIT_CRITICAL(&mux);
    return true;
  }

  bool takeResendRequest(uint16_t *first, uint16_t *count) {
    portENTER_CRITICAL(&mux);
    if (!pendingResend) {
      portEXIT_CRITICAL(&mux);
      return false;
    }
    *first = pendingResendFirst;
    *count = pendingResendCount;
    pendingResend = false;
    portEXIT_CRITICAL(&mux);
    return true;
  }

  void getSequenceStats(uint32_t *skipped, uint32_t *reordered) {
    portENTER_CRITICAL(&mux);
    if (skipped) *skipped = skippedPackets;
    if (reordered) *reordered = reorderedPackets;
    portEXIT_CRITICAL(&mux);
  }

  void getRetransmitStats(uint32_t *requested, uint32_t *received) {
    portENTER_CRITICAL(&mux);
    if (requested) *requested = resendRequests;
    if (received) *received = retransmittedPackets;
    portEXIT_CRITICAL(&mux);
  }

  uint32_t getSequenceResyncs() {
    portENTER_CRITICAL(&mux);
    uint32_t result = sequenceResyncs;
    portEXIT_CRITICAL(&mux);
    return result;
  }
};

static RawPacketRing s_rawPacketRing;

static bool init_airplay_rsa() {
  if (s_airplay_pk_ready) return true;
  mbedtls_pk_init(&s_airplay_pk);
  int ret = mbedtls_pk_parse_key(&s_airplay_pk,
                                 airplay_rsa_private_key_der,
                                 sizeof(airplay_rsa_private_key_der),
                                 NULL, 0);
  if (ret == 0) {
    s_airplay_pk_ready = true;
    Serial.println("[AirPlay] Đã nạp thành công Airport Express RSA Private Key.");
    return true;
  }
  Serial.printf("[AirPlay] Lỗi nạp RSA key: -0x%04X\n", -ret);
  return false;
}

// Cấu hình chân DAC I2S
void vbot_airplay_set_dac_pins(int8_t bclk, int8_t lrc, int8_t dout) {
  airplayDacBclk = bclk;
  airplayDacLrc = lrc;
  airplayDacDout = dout;
}

// Cài đặt driver I2S Stereo 44.1kHz 16-bit
bool vbot_airplay_install_i2s() {
  if (airplayI2sInstalled) return true;
  if (airplayDacBclk < 0 || airplayDacLrc < 0 || airplayDacDout < 0) {
    Serial.println("[AirPlay] Chưa cấu hình chân I2S DAC!");
    return false;
  }

  i2s_driver_uninstall(I2S_NUM_0);

  i2s_config_t i2sConfig = {};
  i2sConfig.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  i2sConfig.sample_rate = 44100;
  i2sConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2sConfig.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT; // Stereo
  i2sConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2sConfig.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2sConfig.dma_buf_count = 8;
  i2sConfig.dma_buf_len = 512;
  i2sConfig.use_apll = false;
  i2sConfig.tx_desc_auto_clear = true;

  i2s_pin_config_t pinConfig = {};
  pinConfig.bck_io_num = airplayDacBclk;
  pinConfig.ws_io_num = airplayDacLrc;
  pinConfig.data_out_num = airplayDacDout;
  pinConfig.data_in_num = I2S_PIN_NO_CHANGE;

  esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2sConfig, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("[AirPlay] Lỗi cài đặt I2S DAC: %d\n", err);
    return false;
  }
  i2s_set_pin(I2S_NUM_0, &pinConfig);
  i2s_zero_dma_buffer(I2S_NUM_0);
  airplayI2sInstalled = true;
  Serial.println("[AirPlay] Đã cài đặt I2S DAC Stereo 44.1kHz thành công.");
  return true;
}

// Gỡ bỏ driver I2S
void vbot_airplay_uninstall_i2s() {
  if (!airplayI2sInstalled) return;
  airplayI2sInstalled = false;
  vTaskDelay(pdMS_TO_TICKS(10));
  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_driver_uninstall(I2S_NUM_0);
  Serial.println("[AirPlay] Đã giải phóng driver I2S DAC.");
}

// Giải mã RSA OAEP cho AES Key
static bool decryptAesKey(const uint8_t *encKey, size_t encLen, uint8_t *outKey) {
  if (!init_airplay_rsa()) return false;

  mbedtls_rsa_context *trsa = mbedtls_pk_rsa(s_airplay_pk);
  if (!trsa) return false;

  mbedtls_rsa_set_padding(trsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA1);

  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctr_drbg);
  const char *pers = "vbot_airplay_aes_key";
  mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char*)pers, strlen(pers));

  size_t olen = 0;
  uint8_t decBuf[256];
  int ret = mbedtls_rsa_pkcs1_decrypt(trsa, mbedtls_ctr_drbg_random, &ctr_drbg, MBEDTLS_RSA_PRIVATE, &olen, encKey, decBuf, sizeof(decBuf));

  mbedtls_ctr_drbg_free(&ctr_drbg);
  mbedtls_entropy_free(&entropy);

  if (ret == 0 && olen == 16) {
    memcpy(outKey, decBuf, 16);
    return true;
  }
  Serial.printf("[AirPlay] Lỗi giải mã RSA OAEP aes key: ret=%d, olen=%u\n", ret, (unsigned)olen);
  return false;
}

// Tạo Apple-Response chứng thực RSA PKCS#1 v1.5
static String computeAppleResponse(const String &challengeB64, IPAddress localIp, const uint8_t mac[6]) {
  if (!init_airplay_rsa()) return "";

  uint8_t challengeRaw[64];
  size_t challengeLen = 0;
  int ret = mbedtls_base64_decode(challengeRaw, sizeof(challengeRaw), &challengeLen,
                                  (const unsigned char*)challengeB64.c_str(), challengeB64.length());
  if (ret != 0 || challengeLen == 0) {
    Serial.printf("[AirPlay] Lỗi base64 decode challenge: %d\n", ret);
    return "";
  }

  uint8_t data[48];
  memset(data, 0, sizeof(data));

  size_t pos = 0;
  size_t copyChall = (challengeLen > 22) ? 22 : challengeLen;
  memcpy(data + pos, challengeRaw, copyChall);
  pos += copyChall;

  uint32_t ipInt = (uint32_t)localIp;
  memcpy(data + pos, &ipInt, 4);
  pos += 4;

  memcpy(data + pos, mac, 6);
  pos += 6;

  size_t dataLen = (pos < 32) ? 32 : pos;

  uint8_t rsaOut[256];
  mbedtls_rsa_context *rsa = mbedtls_pk_rsa(s_airplay_pk);
  mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_NONE);
  ret = mbedtls_rsa_pkcs1_sign(rsa, NULL, NULL, MBEDTLS_RSA_PRIVATE, MBEDTLS_MD_NONE, dataLen, data, rsaOut);
  if (ret != 0) {
    Serial.printf("[AirPlay] Lỗi RSA sign: -0x%04X\n", -ret);
    return "";
  }

  unsigned char b64Out[512];
  size_t b64Len = 0;
  ret = mbedtls_base64_encode(b64Out, sizeof(b64Out), &b64Len, rsaOut, sizeof(rsaOut));
  if (ret != 0) return "";
  b64Out[b64Len] = '\0';

  char *p = strchr((char*)b64Out, '=');
  if (p) *p = '\0';

  return String((char*)b64Out);
}

// Đăng ký dịch vụ mDNS chuẩn RAOP (AirTunes) và AirPlay không mật khẩu cho Apple Control Center
void vbot_airplay_register_mdns() {
  if (!WiFi.isConnected()) return;

  mdns_init();

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macHex[16];
  snprintf(macHex, sizeof(macHex), "%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  String raopInstance = String(macHex) + "@" + airplayFriendlyName;

  mdns_service_remove("_airplay", "_tcp");
  mdns_service_remove("_raop", "_tcp");

  mdns_txt_item_t raopTxt[] = {
    {"txtvers", "1"},
    {"ch", "2"},
    {"cn", "0,1"},
    {"et", "0,1"},
    {"ek", "1"},
    {"sv", "false"},
    {"da", "true"},
    {"sr", "44100"},
    {"ss", "16"},
    {"pw", "false"},
    {"vn", "65537"},
    {"tp", "UDP"},
    {"md", "0,1,2"},
    {"vs", "105.1"},
    {"am", "ShairportSync"},
    {"sf", "0x4"}
  };
  esp_err_t err = mdns_service_add(raopInstance.c_str(), "_raop", "_tcp", AIRPLAY_PORT, raopTxt, sizeof(raopTxt)/sizeof(raopTxt[0]));

  Serial.printf("[AirPlay] Đăng ký Bonjour RAOP (AirPlay 1 Audio): %s (port=%d, code=%d)\n",
                raopInstance.c_str(), AIRPLAY_PORT, (int)err);
}

// Cập nhật tên hiển thị AirPlay theo Client Name và làm mới mDNS
void vbot_airplay_set_name(const char *friendlyName) {
  if (friendlyName && strlen(friendlyName) > 0) {
    airplayFriendlyName = String(friendlyName);
  } else {
    airplayFriendlyName = "VBot Speaker";
  }
  if (WiFi.isConnected() && airplayRunning) {
    vbot_airplay_register_mdns();
  }
}

// Xử lý một kết nối RTSP từ iPhone / Mac
static void handleRtspClient(int clientSock) {
  struct sockaddr_in peerAddr;
  socklen_t peerLen = sizeof(peerAddr);
  if (getpeername(clientSock, (struct sockaddr*)&peerAddr, &peerLen) == 0) {
    s_clientIp = peerAddr.sin_addr;
  }

  char *rxBuf = (char *)malloc(4096);
  char *txBuf = (char *)malloc(2048);
  if (!rxBuf || !txBuf) {
    if (rxBuf) free(rxBuf);
    if (txBuf) free(txBuf);
    close(clientSock);
    return;
  }

  // Bật TCP Keep-Alive để giữ kết nối RTSP khi iPhone khóa màn hình
  int keepAlive = 1;
  setsockopt(clientSock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(keepAlive));

  struct timeval tv;
  tv.tv_sec = 2; // Kiểm tra timeout mỗi 2 giây
  tv.tv_usec = 0;
  setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  while (airplayRunning) {
    int rxLen = recv(clientSock, rxBuf, 4095, 0);
    if (rxLen < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        // Socket timeout định kỳ, iPhone vẫn đang giữ kết nối (ví dụ khi khóa màn hình)
        continue;
      }
      // Lỗi mạng hoặc client ngắt kết nối thực sự
      break;
    }
    if (rxLen == 0) {
      // Client chủ động đóng socket TCP
      break;
    }
    rxBuf[rxLen] = '\0';
    if (strncmp(rxBuf, "OPTIONS", 7) == 0 || strncmp(rxBuf, "ANNOUNCE", 8) == 0 ||
        strncmp(rxBuf, "SETUP", 5) == 0 || strncmp(rxBuf, "RECORD", 6) == 0 ||
        strncmp(rxBuf, "PAUSE", 5) == 0 || strncmp(rxBuf, "TEARDOWN", 8) == 0 ||
        strncmp(rxBuf, "FLUSH", 5) == 0 || strncmp(rxBuf, "SET_PARAMETER", 13) == 0 ||
        strncmp(rxBuf, "GET_PARAMETER", 13) == 0 || strncmp(rxBuf, "POST", 4) == 0) {
#if AIRPLAY_DEBUG_VERBOSE
      char firstLine[64];
      char *crlf = strstr(rxBuf, "\r\n");
      size_t lineLen = crlf ? (size_t)(crlf - rxBuf) : strlen(rxBuf);
      if (lineLen >= sizeof(firstLine)) lineLen = sizeof(firstLine) - 1;
      memcpy(firstLine, rxBuf, lineLen);
      firstLine[lineLen] = '\0';
      Serial.printf("[AirPlay:RX] %s\n", firstLine);
#endif
    }
    String req(rxBuf);

    String cseq = "1";
    int cseqIdx = req.indexOf("CSeq:");
    if (cseqIdx < 0) cseqIdx = req.indexOf("cseq:");
    if (cseqIdx >= 0) {
      int endLine = req.indexOf("\r\n", cseqIdx);
      if (endLine > 0) {
        cseq = req.substring(cseqIdx + 5, endLine);
        cseq.trim();
      }
    }

    if (req.startsWith("OPTIONS")) {
      String appleResp = "";
      int chalIdx = req.indexOf("Apple-Challenge:");
      if (chalIdx < 0) chalIdx = req.indexOf("apple-challenge:");
      if (chalIdx >= 0) {
        int endChal = req.indexOf("\r\n", chalIdx);
        if (endChal > chalIdx) {
          String chal = req.substring(chalIdx + 16, endChal);
          chal.trim();
          uint8_t mac[6];
          WiFi.macAddress(mac);
          appleResp = computeAppleResponse(chal, WiFi.localIP(), mac);
          Serial.printf("[AirPlay] Nhận Apple-Challenge, đã tạo Apple-Response (len=%d)\n", appleResp.length());
        }
      }

      String resp = "RTSP/1.0 200 OK\r\n";
      if (appleResp.length() > 0) {
        resp += "Apple-Response: " + appleResp + "\r\n";
      }
      resp += "CSeq: " + cseq + "\r\n";
      resp += "Server: AirTunes/366.0\r\n";
      resp += "Public: ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, FLUSHBUFFERED, TEARDOWN, OPTIONS, POST, GET, PUT, SET_PARAMETER, GET_PARAMETER\r\n";
      resp += "Content-Length: 0\r\n\r\n";

      send(clientSock, resp.c_str(), resp.length(), 0);
    }
    else if (req.startsWith("ANNOUNCE")) {
      // 1. Phân tích thông số fmtp
      int fmtpIdx = req.indexOf("a=fmtp:");
      if (fmtpIdx >= 0) {
        int endLine = req.indexOf("\r\n", fmtpIdx);
        if (endLine > fmtpIdx) {
          String fmtpLine = req.substring(fmtpIdx + 7, endLine);
          fmtpLine.trim();
          int fmtp[12];
          memset(fmtp, 0, sizeof(fmtp));
          int count = 0;
          char *dup = strdup(fmtpLine.c_str());
          if (dup) {
            char *token = strtok(dup, " \t");
            while (token && count < 12) {
              fmtp[count++] = atoi(token);
              token = strtok(NULL, " \t");
            }
            free(dup);
          }
          if (count >= 12) {
            if (alacDecoder) {
              alac_free(alacDecoder);
              alacDecoder = nullptr;
            }
            if (fmtp[1] <= 0 || fmtp[1] > 4096 || fmtp[3] != 16 || fmtp[11] != 44100) {
              Serial.printf("[AirPlay] Tham số ALAC không hỗ trợ/an toàn: frame=%d, bits=%d, rate=%d.\n",
                            fmtp[1], fmtp[3], fmtp[11]);
            } else {
              alacDecoder = alac_create(fmtp[3], 2);
            }
            if (alacDecoder) {
              alacDecoder->setinfo_max_samples_per_frame = fmtp[1];
              alacDecoder->setinfo_7a = fmtp[2];
              alacDecoder->setinfo_sample_size = fmtp[3];
              alacDecoder->setinfo_rice_historymult = fmtp[4];
              alacDecoder->setinfo_rice_initialhistory = fmtp[5];
              alacDecoder->setinfo_rice_kmodifier = fmtp[6];
              alacDecoder->setinfo_7f = fmtp[7];
              alacDecoder->setinfo_80 = fmtp[8];
              alacDecoder->setinfo_82 = fmtp[9];
              alacDecoder->setinfo_86 = fmtp[10];
              alacDecoder->setinfo_8a_rate = fmtp[11];
              alac_allocate_buffers(alacDecoder);
              if (alacDecoder->allocated_samples == 0) {
                alac_free(alacDecoder);
                alacDecoder = nullptr;
                Serial.println("[AirPlay] Không đủ bộ nhớ tạo decoder ALAC.");
              } else {
                Serial.printf("[AirPlay] Đã khởi tạo ALAC Decoder: frame=%d, rate=%d\n", fmtp[1], fmtp[11]);
              }
            }
          }
        }
      }

      // 2. Phân tích aesiv
      int ivIdx = req.indexOf("a=aesiv:");
      if (ivIdx >= 0) {
        int endLine = req.indexOf("\r\n", ivIdx);
        if (endLine > ivIdx) {
          String ivStr = req.substring(ivIdx + 8, endLine);
          ivStr.trim();
          size_t outLen = 0;
          mbedtls_base64_decode(airplayAesIv, sizeof(airplayAesIv), &outLen,
                                (const unsigned char*)ivStr.c_str(), ivStr.length());
          if (outLen == 16) {
            Serial.println("[AirPlay] Đã trích xuất AES IV thành công.");
          }
        }
      }

      // 3. Phân tích rsaaeskey
      int keyIdx = req.indexOf("a=rsaaeskey:");
      if (keyIdx >= 0) {
        int endLine = req.indexOf("\r\n", keyIdx);
        if (endLine > keyIdx) {
          String keyStr = req.substring(keyIdx + 12, endLine);
          keyStr.trim();
          uint8_t encKey[512];
          size_t encLen = 0;
          int b64Ret = mbedtls_base64_decode(encKey, sizeof(encKey), &encLen,
                                            (const unsigned char*)keyStr.c_str(), keyStr.length());
          if (b64Ret == 0 && encLen > 0) {
            if (decryptAesKey(encKey, encLen, airplayAesKey)) {
              hasAesKey = true;
              if (!aesCtxInited) {
                mbedtls_aes_init(&aesDecCtx);
                aesCtxInited = true;
              }
              mbedtls_aes_setkey_dec(&aesDecCtx, airplayAesKey, 128);
              Serial.println("[AirPlay] Đã giải mã thành công AES-128 Key bằng Airport RSA.");
            }
          }
        }
      }

      snprintf(txBuf, 2048,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %s\r\n"
        "Server: AirTunes/366.0\r\n"
        "Content-Length: 0\r\n\r\n",
        cseq.c_str()
      );
      send(clientSock, txBuf, strlen(txBuf), 0);
    }
    else if (req.startsWith("SETUP")) {
      int cportIdx = req.indexOf("control_port=");
      if (cportIdx >= 0) {
        s_clientControlPort = req.substring(cportIdx + 13).toInt();
      }
      int tportIdx = req.indexOf("timing_port=");
      if (tportIdx >= 0) {
        s_clientTimingPort = req.substring(tportIdx + 12).toInt();
      }

      char resphdr[256];
      snprintf(resphdr, sizeof(resphdr),
        "RTP/AVP/UDP;unicast;interleaved=0-1;mode=record;control_port=%d;timing_port=%d;server_port=%d",
        RTP_CONTROL_PORT, RTP_TIMING_PORT, RTP_AUDIO_PORT
      );

      snprintf(txBuf, 2048,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %s\r\n"
        "Server: AirTunes/366.0\r\n"
        "Transport: %s\r\n"
        "Session: 1\r\n"
        "Audio-Jack-Status: connected; type=analog\r\n"
        "Content-Length: 0\r\n\r\n",
        cseq.c_str(), resphdr
      );
      send(clientSock, txBuf, strlen(txBuf), 0);
      Serial.printf("[AirPlay] Phản hồi SETUP thành công (Control=%d, Timing=%d, Audio=%d -> Client Control=%d, Timing=%d).\n",
                    RTP_CONTROL_PORT, RTP_TIMING_PORT, RTP_AUDIO_PORT, (int)s_clientControlPort, (int)s_clientTimingPort);
    }
    else if (req.startsWith("GET_PARAMETER")) {
      float volDb = -30.0f + (airplayCurrentVolume * 30.0f / 100.0f);
      if (airplayCurrentVolume <= 0) volDb = -144.0f;
      char body[64];
      snprintf(body, sizeof(body), "volume: %.6f\r\n", volDb);

      snprintf(txBuf, 2048,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %s\r\n"
        "Server: AirTunes/366.0\r\n"
        "Content-Type: text/parameters\r\n"
        "Content-Length: %u\r\n\r\n"
        "%s",
        cseq.c_str(), (unsigned int)strlen(body), body
      );
      send(clientSock, txBuf, strlen(txBuf), 0);
      Serial.printf("[AirPlay] Phản hồi GET_PARAMETER (volume: %.1f dB).\n", volDb);
    }
    else if (req.startsWith("RECORD")) {
      s_rawPacketRing.reset();
      s_audioRingBuf.reset();
      s_pcmConcealedFrames = 0;
      s_needRebuffer = true;
      airplayStreaming = true;
      if (airplayCallbacks.onPlay) airplayCallbacks.onPlay();
      if (!s_airplaySuspended) {
        vbot_airplay_install_i2s();
      }
      Serial.println("[AirPlay] iPhone bắt đầu truyền phát âm thanh (RECORD).");

      snprintf(txBuf, 2048,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %s\r\n"
        "Server: AirTunes/366.0\r\n"
        "Session: 1\r\n"
        "Audio-Latency: 88200\r\n\r\n",
        cseq.c_str()
      );
      send(clientSock, txBuf, strlen(txBuf), 0);
    }
    else if (req.startsWith("SET_PARAMETER")) {
      int volIdx = req.indexOf("volume:");
      if (volIdx >= 0) {
        int endLine = req.indexOf("\r\n", volIdx);
        if (endLine > 0) {
          String volStr = req.substring(volIdx + 7, endLine);
          volStr.trim();
          float volDb = volStr.toFloat();
          int volPercent = 0;
          if (volDb >= 0.0f) volPercent = 100;
          else if (volDb <= -144.0f || volDb <= -30.0f) volPercent = (volDb <= -144.0f) ? 0 : 5;
          else volPercent = (int)((volDb + 30.0f) * (100.0f / 30.0f));
          volPercent = constrain(volPercent, 0, 100);

          airplayCurrentVolume = volPercent;
          if (airplayCallbacks.onVolume) airplayCallbacks.onVolume(volPercent);
          Serial.printf("[AirPlay] Điều chỉnh âm lượng từ iPhone: %d%% (%.1f dB)\n", volPercent, volDb);
        }
      }

      snprintf(txBuf, 2048,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %s\r\n"
        "Server: AirTunes/366.0\r\n\r\n",
        cseq.c_str()
      );
      send(clientSock, txBuf, strlen(txBuf), 0);
    }
    else if (req.startsWith("FLUSH") || req.startsWith("FLUSHBUFFERED")) {
      s_rawPacketRing.reset();
      s_audioRingBuf.reset();
      s_pcmConcealedFrames = 0;
      s_needRebuffer = true;
      snprintf(txBuf, 2048,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %s\r\n"
        "Server: AirTunes/366.0\r\n"
        "RTP-Info: seq=0;rtptime=0\r\n"
        "Audio-Latency: 88200\r\n\r\n",
        cseq.c_str()
      );
      send(clientSock, txBuf, strlen(txBuf), 0);
    }
    else if (req.startsWith("PAUSE")) {
      airplayStreaming = false;
      s_rawPacketRing.reset();
      s_audioRingBuf.reset();
      vbot_airplay_uninstall_i2s();
      if (airplayCallbacks.onStop) airplayCallbacks.onStop();
      Serial.println("[AirPlay] iPhone tạm dừng truyền phát (PAUSE).");

      snprintf(txBuf, 2048,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %s\r\n"
        "Server: AirTunes/366.0\r\n\r\n",
        cseq.c_str()
      );
      send(clientSock, txBuf, strlen(txBuf), 0);
    }
    else if (req.startsWith("TEARDOWN")) {
      airplayStreaming = false;
      s_clientTimingPort = 0;
      s_clientControlPort = 0;
      s_needRebuffer = true;
      vTaskDelay(pdMS_TO_TICKS(50));
      s_rawPacketRing.reset();
      s_audioRingBuf.reset();
      vbot_airplay_uninstall_i2s();
      if (airplayCallbacks.onStop) airplayCallbacks.onStop();
      hasAesKey = false;
      Serial.println("[AirPlay] iPhone đã ngắt kết nối (TEARDOWN).");

      snprintf(txBuf, 2048,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %s\r\n"
        "Server: AirTunes/366.0\r\n\r\n",
        cseq.c_str()
      );
      send(clientSock, txBuf, strlen(txBuf), 0);
      break;
    }
    else if (req.startsWith("POST /command")) {
      snprintf(txBuf, 2048,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %s\r\n"
        "Server: AirTunes/366.0\r\n"
        "Content-Length: 0\r\n\r\n",
        cseq.c_str()
      );
      send(clientSock, txBuf, strlen(txBuf), 0);
    }
    else if (req.startsWith("POST /pair-") || req.startsWith("POST /fp-") || req.startsWith("GET /info")) {
      char resp[256];
      snprintf(resp, sizeof(resp),
        "RTSP/1.0 501 Not Implemented\r\n"
        "CSeq: %s\r\n"
        "Server: AirTunes/366.0\r\n"
        "Content-Length: 0\r\n\r\n",
        cseq.c_str()
      );
      send(clientSock, resp, strlen(resp), 0);
    }
    else {
      snprintf(txBuf, 2048,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %s\r\n"
        "Server: AirTunes/366.0\r\n\r\n",
        cseq.c_str()
      );
      send(clientSock, txBuf, strlen(txBuf), 0);
    }
  }

  free(rxBuf);
  free(txBuf);
  close(clientSock);
}

// Task lắng nghe RTSP Server trên cổng 7000
static void vbot_rtsp_task(void *param) {
  int serverSock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (serverSock < 0) {
    Serial.println("[AirPlay] Lỗi tạo RTSP server socket.");
    vTaskDelete(NULL);
    return;
  }

  int enable = 1;
  setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

  struct sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serverAddr.sin_port = htons(AIRPLAY_PORT);

  if (bind(serverSock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
    Serial.printf("[AirPlay] Lỗi bind RTSP port %d.\n", AIRPLAY_PORT);
    close(serverSock);
    vTaskDelete(NULL);
    return;
  }

  if (listen(serverSock, 4) < 0) {
    Serial.println("[AirPlay] Lỗi listen RTSP server.");
    close(serverSock);
    vTaskDelete(NULL);
    return;
  }

  Serial.printf("[AirPlay] Máy chủ RTSP Server đã sẵn sàng trên cổng %d.\n", AIRPLAY_PORT);

  while (airplayRunning) {
    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    int clientSock = accept(serverSock, (struct sockaddr*)&clientAddr, &clientLen);
    if (clientSock >= 0) {
      char clientIp[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, sizeof(clientIp));
      Serial.printf("[AirPlay] Thiết bị đã kết nối từ IP: %s\n", clientIp);
      handleRtspClient(clientSock);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  close(serverSock);
  vTaskDelete(NULL);
}

// Task 1: Hấp thụ gói UDP RTP cực nhanh vào RawPacketRing (Wire-speed Ingest)
static void vbot_audio_udp_task(void *param) {
  int udpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (udpSock < 0) {
    vTaskDelete(NULL);
    return;
  }

  int enable = 1;
  setsockopt(udpSock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

  int rcvBuf = 131072;
  setsockopt(udpSock, SOL_SOCKET, SO_RCVBUF, &rcvBuf, sizeof(rcvBuf));

  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 100000;
  setsockopt(udpSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  struct sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serverAddr.sin_port = htons(RTP_AUDIO_PORT);

  if (bind(udpSock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
    close(udpSock);
    vTaskDelete(NULL);
    return;
  }

  Serial.printf("[AirPlay] Cổng âm thanh RTP Audio UDP đã sẵn sàng trên cổng %d.\n", RTP_AUDIO_PORT);

  uint8_t packetBuf[2048];

  while (airplayRunning) {
    struct sockaddr_in fromAddr;
    socklen_t fromLen = sizeof(fromAddr);
    int len = recvfrom(udpSock, packetBuf, sizeof(packetBuf), 0, (struct sockaddr*)&fromAddr, &fromLen);
    if (len > 12 && (packetBuf[1] & 0x7f) == 0x60) {
      // Đẩy ngay lập tức vào RawPacketRing (chỉ mất ~2 micro-giây) để không rớt gói
      if (s_rawPacketRing.push(packetBuf, len)) {
        s_rtpPacketsReceived++;
        if (audioDecodeTaskHandle) {
          xTaskNotifyGive(audioDecodeTaskHandle);
        }
      } else {
        s_rtpPacketsDropped++;
      }
    }
  }

  close(udpSock);
  vTaskDelete(NULL);
}

// Task xử lý Control (6001) và Timing (6002) đồng bộ thời gian & giữ kết nối với iPhone
static void vbot_airplay_control_task(void *param) {
  int ctrlSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  int timingSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (ctrlSock < 0 || timingSock < 0) {
    if (ctrlSock >= 0) close(ctrlSock);
    if (timingSock >= 0) close(timingSock);
    vTaskDelete(NULL);
    return;
  }

  int enable = 1;
  setsockopt(ctrlSock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
  setsockopt(timingSock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

  struct sockaddr_in ctrlAddr = {};
  ctrlAddr.sin_family = AF_INET;
  ctrlAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  ctrlAddr.sin_port = htons(RTP_CONTROL_PORT);
  bind(ctrlSock, (struct sockaddr*)&ctrlAddr, sizeof(ctrlAddr));

  struct sockaddr_in timingAddr = {};
  timingAddr.sin_family = AF_INET;
  timingAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  timingAddr.sin_port = htons(RTP_TIMING_PORT);
  bind(timingSock, (struct sockaddr*)&timingAddr, sizeof(timingAddr));

  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 5000; // Poll retransmit requests at low latency (5ms).
  setsockopt(ctrlSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(timingSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  Serial.printf("[AirPlay] Cổng Control UDP (%d) và Timing UDP (%d) đã sẵn sàng.\n",
                RTP_CONTROL_PORT, RTP_TIMING_PORT);

  uint8_t buf[2048];
  uint32_t lastTimingPingMs = 0;
  uint16_t resendRequestSeq = 1;

  while (airplayRunning) {
    ulTaskNotifyTake(pdTRUE, 0); // Clear any pending notify from pop()

    // 0. Ưu tiên kiểm tra và gửi ngay retransmit request nếu có gói cần gửi lại
    uint16_t missingFirst = 0;
    uint16_t missingCount = 0;
    if (s_rawPacketRing.takeResendRequest(&missingFirst, &missingCount) &&
        s_clientControlPort > 0 && s_clientIp.s_addr != 0) {
      uint8_t request[8] = {0x80, 0xd5, 0, 0, 0, 0, 0, 0};
      uint16_t netSeq = htons(resendRequestSeq++);
      uint16_t netFirst = htons(missingFirst);
      uint16_t netCount = htons(missingCount);
      memcpy(request + 2, &netSeq, sizeof(netSeq));
      memcpy(request + 4, &netFirst, sizeof(netFirst));
      memcpy(request + 6, &netCount, sizeof(netCount));
      struct sockaddr_in resendDest = {};
      resendDest.sin_family = AF_INET;
      resendDest.sin_addr = s_clientIp;
      resendDest.sin_port = htons(s_clientControlPort);
      sendto(ctrlSock, request, sizeof(request), 0,
             (struct sockaddr*)&resendDest, sizeof(resendDest));
    }

    // 1. Nhận trên cổng Control (6001): Sync packets (0xd4) hoặc Retransmitted audio (0xd6)
    struct sockaddr_in fromAddr;
    socklen_t fromLen = sizeof(fromAddr);
    int len = recvfrom(ctrlSock, buf, sizeof(buf), 0, (struct sockaddr*)&fromAddr, &fromLen);
    if (len > 0) {
      if (buf[1] == 0xd6 && len > 16) {
        // Gói audio gửi lại từ iPhone qua kênh control: bỏ 4 bytes header đầu
        if (s_rawPacketRing.push(buf + 4, len - 4, true)) {
          if (audioDecodeTaskHandle) xTaskNotifyGive(audioDecodeTaskHandle);
        }
      }
    }

    // 2. Nhận trên cổng Timing (6002):
    fromLen = sizeof(fromAddr);
    len = recvfrom(timingSock, buf, sizeof(buf), 0, (struct sockaddr*)&fromAddr, &fromLen);
    if (len >= (int)sizeof(AirPlayTimingPacket)) {
      if (buf[1] == 0xd2) {
        // iPhone gửi timing request tới chúng ta -> trả lời timing reply 0xd3
        buf[1] = 0xd3;
        sendto(timingSock, buf, len, 0, (struct sockaddr*)&fromAddr, fromLen);
      }
    }

    // 3. Gửi Timing Ping keepalive định kỳ (mỗi 2.0 giây) đến iPhone khi đang stream
    uint32_t now = millis();
    if (airplayStreaming && s_clientTimingPort > 0 && s_clientIp.s_addr != 0) {
      if (now - lastTimingPingMs >= 2000) {
        lastTimingPingMs = now;
        AirPlayTimingPacket req = {};
        req.leader = 0x80;
        req.type = 0xd2;
        req.seqno = htons(7);

        struct sockaddr_in dest = {};
        dest.sin_family = AF_INET;
        dest.sin_addr = s_clientIp;
        dest.sin_port = htons(s_clientTimingPort);
        sendto(timingSock, &req, sizeof(req), 0, (struct sockaddr*)&dest, sizeof(dest));
      }
    }
  }

  close(ctrlSock);
  close(timingSock);
  vTaskDelete(NULL);
}

// Task 2: Chuyên giải mã ALAC từ RawPacketRing sang AudioRingBuffer
static void vbot_airplay_decode_task(void *param) {
  RawAudioPacket rawPkt;
  int16_t previousLeft = 0;
  int16_t previousRight = 0;
  bool inConcealment = false;
  uint8_t *decryptedBuf = (uint8_t*)ps_malloc(4098);
  if (!decryptedBuf) decryptedBuf = (uint8_t*)malloc(4098);
  int16_t *pcmOutBuf = (int16_t*)ps_malloc(16384); // ALAC maximum: 4096 samples x stereo x 16-bit
  if (!pcmOutBuf) pcmOutBuf = (int16_t*)malloc(16384);

  if (!decryptedBuf || !pcmOutBuf) {
    Serial.println("[AirPlay] Không đủ bộ nhớ cho bộ đệm giải mã; dừng task giải mã.");
    if (decryptedBuf) free(decryptedBuf);
    if (pcmOutBuf) free(pcmOutBuf);
    audioDecodeTaskHandle = nullptr;
    vTaskDelete(NULL);
    return;
  }

  while (airplayRunning) {
    if (!airplayStreaming || !alacDecoder) {
      vTaskDelay(pdMS_TO_TICKS(15));
      continue;
    }

    bool processedAny = false;
    while (true) {
      if (s_rawPacketRing.takeConcealmentFrame()) {
        size_t silenceBytes = (size_t)alacDecoder->setinfo_max_samples_per_frame *
                              (size_t)alacDecoder->bytespersample;
        if (silenceBytes > 0 && silenceBytes <= 16384) {
          memset(pcmOutBuf, 0, silenceBytes);
          if (!inConcealment && alacDecoder->numchannels == 2 &&
              alacDecoder->setinfo_sample_size == 16) {
            int16_t *silence = (int16_t*)pcmOutBuf;
            size_t frames = silenceBytes / (2 * sizeof(int16_t));
            size_t fadeFrames = frames < 32 ? frames : 32;
            for (size_t i = 0; i < fadeFrames; ++i) {
              int32_t gain = (int32_t)(fadeFrames - 1 - i);
              int32_t denom = fadeFrames > 1 ? (int32_t)(fadeFrames - 1) : 1;
              silence[i * 2] = (int16_t)(((int32_t)previousLeft * gain) / denom);
              silence[i * 2 + 1] = (int16_t)(((int32_t)previousRight * gain) / denom);
            }
          }
          size_t queued = s_audioRingBuf.write((const uint8_t*)pcmOutBuf, silenceBytes);
          if (queued == silenceBytes) {
            s_pcmConcealedFrames++;
            inConcealment = true;
          }
        }
        processedAny = true;
        continue;
      }
      size_t curAudioAvail = s_audioRingBuf.available();
      if (!s_rawPacketRing.pop(&rawPkt, curAudioAvail)) break;
      processedAny = true;
      if (rawPkt.len > 12) {
        uint8_t type = rawPkt.data[1] & ~0x80;
        if (type == 0x54) {
          continue; // Sync packet
        }
        if (type == 0x60 || type == 0x56) {
          int offset = (type == 0x56) ? 16 : 12;
          int payloadLen = rawPkt.len - offset;

          if (payloadLen > 0) {
            if (payloadLen > 4096) continue;
            uint8_t *payloadPtr = rawPkt.data + offset;
            // The bit reader loads three bytes at a time; keep two zero guard bytes.
            memset(decryptedBuf, 0, 4098);

            // 1. Giải mã AES
            if (hasAesKey) {
              int aeslen = payloadLen & ~0x0F;
              uint8_t iv[16];
              memcpy(iv, airplayAesIv, 16);
              mbedtls_aes_crypt_cbc(&aesDecCtx, MBEDTLS_AES_DECRYPT, aeslen, iv, payloadPtr, decryptedBuf);
              if (payloadLen > aeslen) {
                memcpy(decryptedBuf + aeslen, payloadPtr + aeslen, payloadLen - aeslen);
              }
            } else {
              memcpy(decryptedBuf, payloadPtr, payloadLen);
            }

            // 2. Giải mã ALAC frame
            s_isDecoding = true;
            int outsize = 0;
            if (alacDecoder && alacDecoder->allocated_samples > 0 && pcmOutBuf) {
              alac_decode_frame(alacDecoder, decryptedBuf, (size_t)payloadLen, pcmOutBuf, &outsize);
            }
            s_isDecoding = false;

            // 3. Nạp PCM vào ring buffer; xử lý volume ở giai đoạn phát.
            if (outsize > 0 && outsize <= 16384) {
              if (inConcealment && alacDecoder->numchannels == 2 &&
                  alacDecoder->setinfo_sample_size == 16) {
                int16_t *samples = (int16_t*)pcmOutBuf;
                size_t frames = (size_t)outsize / (2 * sizeof(int16_t));
                size_t fadeFrames = frames < 32 ? frames : 32;
                for (size_t i = 0; i < fadeFrames; ++i) {
                  int32_t gain = (int32_t)(i + 1);
                  samples[i * 2] = (int16_t)(((int32_t)samples[i * 2] * gain) / 32);
                  samples[i * 2 + 1] = (int16_t)(((int32_t)samples[i * 2 + 1] * gain) / 32);
                }
              }
              size_t queued = s_audioRingBuf.write((const uint8_t*)pcmOutBuf, outsize);
              if (queued == (size_t)outsize) {
                s_alacFramesDecoded++;
                s_pcmBytesDecoded += (uint32_t)outsize;
                if (alacDecoder->numchannels == 2 && alacDecoder->setinfo_sample_size == 16) {
                  int16_t *samples = (int16_t*)pcmOutBuf;
                  size_t frames = (size_t)outsize / (2 * sizeof(int16_t));
                  if (frames > 0) {
                    previousLeft = samples[(frames - 1) * 2];
                    previousRight = samples[(frames - 1) * 2 + 1];
                  }
                }
                inConcealment = false;
              }
            }
          }
        }
      }
    }

    if (!processedAny) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
    }
  }

  vTaskDelete(NULL);
}

// Task 3: Đọc từ Jitter RingBuffer và phát ra I2S DAC với nhịp clock chuẩn 44.1kHz
static void vbot_airplay_play_task(void *param) {
  uint8_t pcmChunk[1408];
  bool isBuffering = true;
  uint32_t underrunTotal = 0;
  uint32_t lastStatMs = 0;
  uint32_t lastRxPackets = 0;
  uint32_t lastDroppedPackets = 0;
  uint32_t lastDecodedFrames = 0;
  uint32_t lastDecodedBytes = 0;

  while (airplayRunning) {
    // 1. Kiểm tra xem VBot Assistant có đang bận (thu âm, xử lý, phát ding/TTS) hoặc bị suspend không
    bool assistantBusy = (airplayCallbacks.isVBotBusy && airplayCallbacks.isVBotBusy());
    if (assistantBusy) {
      s_airplaySuspended = true;
    }

    if (s_airplaySuspended) {
      if (airplayI2sInstalled) {
        Serial.println("\n[AirPlay] >>> Tạm dừng nhạc AirPlay. Trả lại DAC và Mic cho VBot Assistant!");
        vbot_airplay_uninstall_i2s();
        if (airplayCallbacks.onStop) airplayCallbacks.onStop();
        if (airplayCallbacks.onResumeMic) airplayCallbacks.onResumeMic();
      }
      // Dọn sạch đệm PCM để khi resume không bị phát dồn nhạc cũ bị trễ
      s_audioRingBuf.reset();
      s_rawPacketRing.reset();
      s_needRebuffer = true;
      isBuffering = true;

      // Chỉ hủy cờ suspend khi VBot Assistant đã xong hoàn toàn
      if (!assistantBusy) {
        vTaskDelay(pdMS_TO_TICKS(250));
        // Kiểm tra lại lần nữa sau delay để đảm bảo không bị race condition
        if (airplayCallbacks.isVBotBusy && airplayCallbacks.isVBotBusy()) {
          continue;
        }
        if (airplayStreaming && !airplayI2sInstalled) {
          Serial.println("\n[AirPlay] >>> VBot Assistant đã xong. Khôi phục phát nhạc AirPlay!");
          vbot_airplay_install_i2s();
          s_airplaySuspended = false;
          if (airplayCallbacks.onPlay) airplayCallbacks.onPlay();
        } else {
          s_airplaySuspended = false;
        }
      } else {
        vTaskDelay(pdMS_TO_TICKS(50));
        continue;
      }
    }

    if (!airplayStreaming || !airplayI2sInstalled) {
      isBuffering = true;
      s_needRebuffer = true;
      vTaskDelay(pdMS_TO_TICKS(15));
      continue;
    }

    size_t avail = s_audioRingBuf.available();
    if (!airplayStreaming) {
      isBuffering = true;
      vTaskDelay(pdMS_TO_TICKS(15));
      continue;
    }

#if AIRPLAY_DEBUG_VERBOSE
    // Thống kê mỗi 2 giây
    uint32_t now = millis();
    if (now - lastStatMs >= 2000) {
      lastStatMs = now;
      uint32_t rxPackets = s_rtpPacketsReceived;
      uint32_t droppedPackets = s_rtpPacketsDropped;
      uint32_t decodedFrames = s_alacFramesDecoded;
      uint32_t decodedBytes = s_pcmBytesDecoded;
      uint32_t skippedPackets = 0;
      uint32_t reorderedPackets = 0;
      uint32_t resendRequests = 0;
      uint32_t retransmittedPackets = 0;
      uint32_t concealedFrames = s_pcmConcealedFrames;
      uint32_t sequenceResyncs = s_rawPacketRing.getSequenceResyncs();
      s_rawPacketRing.getSequenceStats(&skippedPackets, &reorderedPackets);
      s_rawPacketRing.getRetransmitStats(&resendRequests, &retransmittedPackets);
      Serial.printf("[AirPlay:Stats] Buf:%u/%u KB (%.2f s), JitQ:%u, RTP:%u/s drop:%u/s reorder:%u skip:%u req:%u retx:%u resync:%u conceal:%u, ALAC:%u/s PCM:%u KB/s, Underruns:%u\n",
                    (unsigned)(avail / 1024), (unsigned)(AIRPLAY_RINGBUF_SIZE / 1024),
                    (float)avail / 176400.0f, (unsigned)s_rawPacketRing.count(),
                    (unsigned)((rxPackets - lastRxPackets) / 2),
                    (unsigned)((droppedPackets - lastDroppedPackets) / 2),
                    (unsigned)reorderedPackets, (unsigned)skippedPackets,
                    (unsigned)resendRequests, (unsigned)retransmittedPackets,
                    (unsigned)sequenceResyncs,
                    (unsigned)concealedFrames,
                    (unsigned)((decodedFrames - lastDecodedFrames) / 2),
                    (unsigned)((decodedBytes - lastDecodedBytes) / 2048),
                    (unsigned)underrunTotal);
      lastRxPackets = rxPackets;
      lastDroppedPackets = droppedPackets;
      lastDecodedFrames = decodedFrames;
      lastDecodedBytes = decodedBytes;
    }
#endif

    // Start rebuffering before I2S starves; wait for a meaningful recovery window.
    if (!isBuffering && avail < AIRPLAY_LOW_WATERMARK) {
      isBuffering = true;
      underrunTotal++;
    }

    // Rebuffer to a separate high watermark so playback does not flap around the low watermark.
    if (s_needRebuffer) {
      if (avail >= AIRPLAY_INITIAL_BUFFER) {
        s_needRebuffer = false;
        isBuffering = false;
      } else {
        vTaskDelay(pdMS_TO_TICKS(2));
        continue;
      }
    } else if (isBuffering) {
      if (avail >= AIRPLAY_RECOVERY_BUFFER) {
        isBuffering = false;
      } else {
        vTaskDelay(pdMS_TO_TICKS(2));
        continue;
      }
    }

    // 3. Adaptive Drift Control: Điều tiết nhịp đọc khi đệm an toàn
    // Không delay khi đệm đang thấp để tránh làm trễ xả buffer ra DAC
    if (avail < 16 * 1024 && !isBuffering) {
      // Cho phép phát tiếp tục mà không chèn delay nhân tạo
    }

    if (avail > 0) {
      size_t toRead = (avail >= sizeof(pcmChunk)) ? sizeof(pcmChunk) : avail;
      toRead &= ~3;
      if (toRead == 0) {
        vTaskDelay(pdMS_TO_TICKS(2));
        continue;
      }

      size_t readBytes = s_audioRingBuf.read(pcmChunk, toRead);
      if (readBytes > 0) {
        int volume = airplayCurrentVolume;
        if (volume < 100) {
          int16_t *samples = (int16_t*)pcmChunk;
          size_t sampleCount = readBytes / sizeof(int16_t);
          for (size_t i = 0; i < sampleCount; ++i) {
            samples[i] = (int16_t)(((int32_t)samples[i] * volume) / 100);
          }
        }

        if (airplayCallbacks.onPeak && !s_airplaySuspended) {
          int16_t *samples = (int16_t*)pcmChunk;
          int sampleCount = readBytes / sizeof(int16_t);
          uint16_t maxPeak = 0;
          for (int i = 0; i < sampleCount; i += 2) {
            int32_t val = abs(samples[i]);
            if (val > maxPeak) maxPeak = (uint16_t)val;
          }
          // Chuẩn hóa theo âm lượng hiện tại: nếu volume < 100%, bù lại để visualizer LED phản hồi chính xác tiết tấu bài hát
          if (airplayCurrentVolume > 0 && airplayCurrentVolume < 100) {
            uint32_t boosted = ((uint32_t)maxPeak * 100) / airplayCurrentVolume;
            if (boosted > 32767) boosted = 32767;
            maxPeak = (uint16_t)boosted;
          }
          airplayCallbacks.onPeak(maxPeak);
        }

        size_t written = 0;
        if (airplayI2sInstalled && !s_airplaySuspended) {
          i2s_write(I2S_NUM_0, pcmChunk, readBytes, &written, pdMS_TO_TICKS(100));
        }
      }
    } else {
      // Tránh busy-loop khi buffer tạm rỗng nhưng chưa đạt mức underrun
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }

  vTaskDelete(NULL);
}

bool vbot_airplay_init(const char *friendlyName, const AirPlayCallbacks &callbacks) {
  if (airplayRunning) return true;

  airplayCallbacks = callbacks;
  if (friendlyName && strlen(friendlyName) > 0) {
    airplayFriendlyName = String(friendlyName);
  } else {
    airplayFriendlyName = "VBot Speaker";
  }

  // 1. Khởi tạo các bộ đệm trên PSRAM
  if (!s_audioRingBuf.init(AIRPLAY_RINGBUF_SIZE)) {
    Serial.println("[AirPlay] Không cấp phát được audio ring buffer; AirPlay chưa khởi chạy.");
    return false;
  }
  if (!s_rawPacketRing.init(512)) { // 512 gói UDP raw (~4 giây)
    Serial.println("[AirPlay] Không cấp phát được RTP packet queue; AirPlay chưa khởi chạy.");
    s_audioRingBuf.freeBuffer();
    return false;
  }

  airplayRunning = true;

  // 2. Đăng ký mDNS Bonjour
  vbot_airplay_register_mdns();

  // 3. Chạy RTSP Server Task
  xTaskCreatePinnedToCore(
    vbot_rtsp_task,
    "vbot_rtsp",
    8192,
    NULL,
    1,
    &rtspTaskHandle,
    0
  );

  // 4. Chạy UDP Ingest Task trên Core 0 (ưu tiên 6 cực cao, hấp thụ gói tức thì không rớt)
  xTaskCreatePinnedToCore(
    vbot_audio_udp_task,
    "vbot_airplay_udp",
    8192,
    NULL,
    6,
    &audioUdpTaskHandle,
    0 // Chạy trên Core 0 chung với WiFi Stack để không có độ trễ cross-core
  );

  // 5. Chạy ALAC Decode Task trên Core 1 (ưu tiên 5, giải mã ALAC ưu tiên cao hơn play để không bị thiếu dữ liệu)
  xTaskCreatePinnedToCore(
    vbot_airplay_decode_task,
    "vbot_airplay_dec",
    8192,
    NULL,
    5,
    &audioDecodeTaskHandle,
    1 // Chạy trên Core 1
  );

  // 6. Chạy Audio I2S Playout Task trên Core 1 (ưu tiên 4, nạp DMA I2S - thấp hơn decode)
  xTaskCreatePinnedToCore(
    vbot_airplay_play_task,
    "vbot_airplay_play",
    4096,
    NULL,
    4,
    &audioPlayTaskHandle,
    1 // Chạy trên Core 1
  );

  // 7. Chạy UDP Control & Timing Task trên Core 0 (đáp ứng Sync và gửi Timing Ping keepalive)
  xTaskCreatePinnedToCore(
    vbot_airplay_control_task,
    "vbot_airplay_ctl",
    4096,
    NULL,
    3,
    &audioControlTaskHandle,
    0
  );

  Serial.printf("[AirPlay] Đã khởi tạo kiến trúc 4-Stage Pipeline (RawQueue=512, AudioBuf=512KB, Control+Timing): %s\n", airplayFriendlyName.c_str());
  return true;
}

void vbot_airplay_stop() {
  if (!airplayRunning) return;
  airplayRunning = false;
  airplayStreaming = false;

  vbot_airplay_uninstall_i2s();
  s_rawPacketRing.reset();
  s_rawPacketRing.freeBuffer();
  s_audioRingBuf.reset();
  s_audioRingBuf.freeBuffer();

  mdns_service_remove("_raop", "_tcp");
  mdns_service_remove("_airplay", "_tcp");

  if (audioPlayTaskHandle) {
    vTaskDelete(audioPlayTaskHandle);
    audioPlayTaskHandle = nullptr;
  }
  if (audioDecodeTaskHandle) {
    vTaskDelete(audioDecodeTaskHandle);
    audioDecodeTaskHandle = nullptr;
  }
  if (audioControlTaskHandle) {
    vTaskDelete(audioControlTaskHandle);
    audioControlTaskHandle = nullptr;
  }
  if (audioUdpTaskHandle) {
    vTaskDelete(audioUdpTaskHandle);
    audioUdpTaskHandle = nullptr;
  }
  if (rtspTaskHandle) {
    vTaskDelete(rtspTaskHandle);
    rtspTaskHandle = nullptr;
  }

  if (alacDecoder) {
    alac_free(alacDecoder);
    alacDecoder = nullptr;
  }
  hasAesKey = false;

  Serial.println("[AirPlay] Đã dừng Apple AirPlay Receiver.");
}

bool vbot_airplay_is_running() {
  return airplayRunning;
}

void vbot_airplay_suspend() {
  s_airplaySuspended = true;
}

void vbot_airplay_resume() {
  s_airplaySuspended = false;
}

bool vbot_airplay_is_suspended() {
  return s_airplaySuspended;
}

bool vbot_airplay_is_active() {
  return airplayStreaming;
}

bool vbot_airplay_is_streaming() {
  return airplayStreaming && !s_airplaySuspended && airplayI2sInstalled;
}

int vbot_airplay_get_volume() {
  return airplayCurrentVolume;
}
