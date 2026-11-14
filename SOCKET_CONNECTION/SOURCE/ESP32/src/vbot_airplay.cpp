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

static bool airplayRunning = false;
static volatile bool airplayStreaming = false;
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
constexpr size_t AIRPLAY_INITIAL_BUFFER = 96 * 1024;   // ~550ms đệm khởi đầu mượt mà
constexpr size_t AIRPLAY_UNDERRUN_BUFFER = 48 * 1024;  // ~278ms phục hồi chống ngắt quãng khi iPhone khóa màn hình

struct AudioRingBuffer {
  uint8_t *buffer = nullptr;
  size_t capacity = 0;
  volatile size_t head = 0;
  volatile size_t tail = 0;
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

  bool init(size_t size) {
    if (buffer) free(buffer);
    capacity = size;
    head = tail = 0;
    if (psramFound()) {
      buffer = (uint8_t*)ps_malloc(capacity);
    }
    if (!buffer) {
      buffer = (uint8_t*)malloc(capacity);
    }
    return buffer != nullptr;
  }

  void freeBuffer() {
    if (buffer) {
      free(buffer);
      buffer = nullptr;
    }
    head = tail = 0;
  }

  void reset() {
    portENTER_CRITICAL(&mux);
    head = tail = 0;
    portEXIT_CRITICAL(&mux);
  }

  size_t available() {
    portENTER_CRITICAL(&mux);
    size_t h = head;
    size_t t = tail;
    portEXIT_CRITICAL(&mux);
    if (h >= t) return h - t;
    return capacity - (t - h);
  }

  size_t freeSpace() {
    return capacity - 1 - available();
  }

  size_t write(const uint8_t *data, size_t len) {
    if (!buffer || len == 0) return 0;
    size_t space = freeSpace();
    if (len > space) len = space;
    if (len == 0) return 0;

    size_t h = head;
    size_t firstPart = (h + len <= capacity) ? len : (capacity - h);
    memcpy(buffer + h, data, firstPart);
    if (len > firstPart) {
      memcpy(buffer, data + firstPart, len - firstPart);
    }
    portENTER_CRITICAL(&mux);
    head = (h + len) % capacity;
    portEXIT_CRITICAL(&mux);
    return len;
  }

  size_t read(uint8_t *out, size_t len) {
    if (!buffer || len == 0) return 0;
    size_t avail = available();
    if (len > avail) len = avail;
    if (len == 0) return 0;

    size_t t = tail;
    size_t firstPart = (t + len <= capacity) ? len : (capacity - t);
    memcpy(out, buffer + t, firstPart);
    if (len > firstPart) {
      memcpy(out + firstPart, buffer, len - firstPart);
    }
    portENTER_CRITICAL(&mux);
    tail = (t + len) % capacity;
    portEXIT_CRITICAL(&mux);
    return len;
  }
};

static AudioRingBuffer s_audioRingBuf;

struct RawAudioPacket {
  uint16_t len;
  uint8_t data[1500];
};

struct RawPacketRing {
  RawAudioPacket *packets = nullptr;
  size_t capacity = 256; // Đệm 256 gói UDP raw (~2 giây)
  volatile size_t head = 0;
  volatile size_t tail = 0;
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

  bool init(size_t cap = 256) {
    capacity = cap;
    head = tail = 0;
    if (packets) free(packets);
    if (psramFound()) {
      packets = (RawAudioPacket*)ps_malloc(capacity * sizeof(RawAudioPacket));
    }
    if (!packets) {
      packets = (RawAudioPacket*)malloc(capacity * sizeof(RawAudioPacket));
    }
    return packets != nullptr;
  }

  void freeBuffer() {
    if (packets) {
      free(packets);
      packets = nullptr;
    }
    head = tail = 0;
  }

  void reset() {
    portENTER_CRITICAL(&mux);
    head = tail = 0;
    portEXIT_CRITICAL(&mux);
  }

  size_t count() {
    portENTER_CRITICAL(&mux);
    size_t h = head;
    size_t t = tail;
    portEXIT_CRITICAL(&mux);
    if (h >= t) return h - t;
    return capacity - (t - h);
  }

  bool push(const uint8_t *data, size_t len) {
    if (!packets || len == 0 || len > 1500) return false;
    portENTER_CRITICAL(&mux);
    size_t nextHead = (head + 1) % capacity;
    if (nextHead == tail) {
      portEXIT_CRITICAL(&mux);
      return false; // Queue đầy
    }
    size_t curHead = head;
    head = nextHead;
    portEXIT_CRITICAL(&mux);

    packets[curHead].len = (uint16_t)len;
    memcpy(packets[curHead].data, data, len);
    return true;
  }

  bool pop(RawAudioPacket *out) {
    if (!packets || !out) return false;
    portENTER_CRITICAL(&mux);
    if (head == tail) {
      portEXIT_CRITICAL(&mux);
      return false; // Rỗng
    }
    size_t curTail = tail;
    tail = (tail + 1) % capacity;
    portEXIT_CRITICAL(&mux);

    *out = packets[curTail];
    return true;
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
  i2sConfig.dma_buf_count = 6;
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
  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_driver_uninstall(I2S_NUM_0);
  airplayI2sInstalled = false;
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

// Đăng ký dịch vụ mDNS chuẩn RAOP (AirTunes) không mật khẩu cho Apple Control Center
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

  Serial.printf("[AirPlay] Đăng ký Bonjour RAOP không mật khẩu: %s (port=%d, code=%d)\n",
                raopInstance.c_str(), AIRPLAY_PORT, (int)err);
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
      char firstLine[64];
      char *crlf = strstr(rxBuf, "\r\n");
      size_t lineLen = crlf ? (size_t)(crlf - rxBuf) : strlen(rxBuf);
      if (lineLen >= sizeof(firstLine)) lineLen = sizeof(firstLine) - 1;
      memcpy(firstLine, rxBuf, lineLen);
      firstLine[lineLen] = '\0';
      Serial.printf("[AirPlay:RX] %s\n", firstLine);
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
            alacDecoder = alac_create(fmtp[3] > 0 ? fmtp[3] : 16, 2);
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
              Serial.printf("[AirPlay] Đã khởi tạo ALAC Decoder: frame=%d, rate=%d\n", fmtp[1], fmtp[11]);
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
      s_needRebuffer = true;
      airplayStreaming = true;
      if (airplayCallbacks.onPlay) airplayCallbacks.onPlay();
      vbot_airplay_install_i2s();
      Serial.println("[AirPlay] iPhone bắt đầu truyền phát âm thanh (RECORD).");

      snprintf(txBuf, 2048,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %s\r\n"
        "Server: AirTunes/366.0\r\n"
        "Session: 1\r\n"
        "Audio-Latency: 44100\r\n\r\n",
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
      s_needRebuffer = true;
      if (airplayI2sInstalled) {
        i2s_zero_dma_buffer(I2S_NUM_0);
      }
      snprintf(txBuf, 2048,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %s\r\n"
        "Server: AirTunes/366.0\r\n"
        "RTP-Info: seq=0;rtptime=0\r\n\r\n",
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
    if (len > 12) {
      // Đẩy ngay lập tức vào RawPacketRing (chỉ mất ~2 micro-giây) để không rớt gói
      if (s_rawPacketRing.push(packetBuf, len)) {
        if (audioDecodeTaskHandle) {
          xTaskNotifyGive(audioDecodeTaskHandle);
        }
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
  tv.tv_usec = 200000; // 200ms timeout
  setsockopt(ctrlSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(timingSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  Serial.printf("[AirPlay] Cổng Control UDP (%d) và Timing UDP (%d) đã sẵn sàng.\n",
                RTP_CONTROL_PORT, RTP_TIMING_PORT);

  uint8_t buf[2048];
  uint32_t lastTimingPingMs = 0;

  while (airplayRunning) {
    // 1. Nhận trên cổng Control (6001): Sync packets (0xd4) hoặc Retransmitted audio (0xd6)
    struct sockaddr_in fromAddr;
    socklen_t fromLen = sizeof(fromAddr);
    int len = recvfrom(ctrlSock, buf, sizeof(buf), 0, (struct sockaddr*)&fromAddr, &fromLen);
    if (len > 0) {
      if (buf[1] == 0xd6 && len > 16) {
        // Gói audio gửi lại từ iPhone qua kênh control: bỏ 4 bytes header đầu
        if (s_rawPacketRing.push(buf + 4, len - 4)) {
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
  uint8_t *decryptedBuf = (uint8_t*)ps_malloc(4096);
  if (!decryptedBuf) decryptedBuf = (uint8_t*)malloc(4096);
  int16_t *pcmOutBuf = (int16_t*)ps_malloc(8192); // 4096 samples stereo 16-bit
  if (!pcmOutBuf) pcmOutBuf = (int16_t*)malloc(8192);

  while (airplayRunning) {
    if (!airplayStreaming || !alacDecoder) {
      vTaskDelay(pdMS_TO_TICKS(15));
      continue;
    }

    bool processedAny = false;
    while (s_rawPacketRing.pop(&rawPkt)) {
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
            uint8_t *payloadPtr = rawPkt.data + offset;

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
            if (alacDecoder) {
              alac_decode_frame(alacDecoder, decryptedBuf, pcmOutBuf, &outsize);
            }
            s_isDecoding = false;

            // 3. Áp dụng âm lượng và nạp vào AudioRingBuffer
            if (outsize > 0) {
              int16_t *samples = (int16_t*)pcmOutBuf;
              int sampleCount = outsize / sizeof(int16_t);
              int vol = airplayCurrentVolume;

              if (vol < 100) {
                for (int i = 0; i < sampleCount; i++) {
                  samples[i] = (int16_t)(((int32_t)samples[i] * vol) / 100);
                }
              }

              s_audioRingBuf.write((const uint8_t*)pcmOutBuf, outsize);
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

  while (airplayRunning) {
    if (!airplayStreaming || !airplayI2sInstalled) {
      isBuffering = true;
      s_needRebuffer = true;
      vTaskDelay(pdMS_TO_TICKS(15));
      continue;
    }

    size_t avail = s_audioRingBuf.available();

    // Thống kê mỗi 2 giây
    uint32_t now = millis();
    if (now - lastStatMs >= 2000) {
      lastStatMs = now;
      Serial.printf("[AirPlay:Stats] Buf: %u/%u KB (%.2f s), RawQ: %u pkts, Underruns: %u\n",
                    (unsigned)(avail / 1024), (unsigned)(AIRPLAY_RINGBUF_SIZE / 1024),
                    (float)avail / 176400.0f, (unsigned)s_rawPacketRing.count(), (unsigned)underrunTotal);
    }

    // 1. Nếu cạn đệm: chuyển sang chế độ gom, xả DMA tĩnh lặng tránh bụp
    if (!isBuffering && avail == 0) {
      isBuffering = true;
      underrunTotal++;
      if (airplayI2sInstalled) {
        i2s_zero_dma_buffer(I2S_NUM_0);
      }
    }

    // 2. Chế độ gom đệm: lần đầu (hoặc sau FLUSH) gom đủ INITIAL_BUFFER (96KB), sau đó gom UNDERRUN_BUFFER (48KB)
    if (s_needRebuffer) {
      if (avail >= AIRPLAY_INITIAL_BUFFER) {
        s_needRebuffer = false;
        isBuffering = false;
      } else {
        vTaskDelay(pdMS_TO_TICKS(2));
        continue;
      }
    } else if (isBuffering) {
      if (avail >= AIRPLAY_UNDERRUN_BUFFER) {
        isBuffering = false;
      } else {
        vTaskDelay(pdMS_TO_TICKS(2));
        continue;
      }
    }

    // 3. Adaptive Drift Control: Chống trôi clock khi ESP32 tiêu thụ nhanh hơn tốc độ iPhone gửi
    // Giữ cho bộ đệm luôn ổn định quanh mức an toàn (> 32 KB), ngăn chặn cạn đệm từ gốc
    if (avail < 32 * 1024) {
      vTaskDelay(pdMS_TO_TICKS(1));
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
        if (airplayCallbacks.onPeak) {
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
        i2s_write(I2S_NUM_0, pcmChunk, readBytes, &written, portMAX_DELAY);
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
  s_audioRingBuf.init(AIRPLAY_RINGBUF_SIZE);
  s_rawPacketRing.init(256); // 256 gói UDP raw (~2 giây)

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

  Serial.printf("[AirPlay] Đã khởi tạo kiến trúc 4-Stage Pipeline (RawQueue=256, AudioBuf=512KB, Control+Timing): %s\n", airplayFriendlyName.c_str());
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

bool vbot_airplay_is_streaming() {
  return airplayStreaming;
}

int vbot_airplay_get_volume() {
  return airplayCurrentVolume;
}
