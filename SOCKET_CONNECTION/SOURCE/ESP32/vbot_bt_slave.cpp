#include "vbot_bt_slave.h"
#include "driver/i2s.h"

static constexpr i2s_port_t BT_SLAVE_I2S_PORT = I2S_NUM_1;
static constexpr i2s_port_t BT_DAC_I2S_PORT   = I2S_NUM_0;

static int8_t s_slaveBclk = 14;
static int8_t s_slaveWs   = 21;
static int8_t s_slaveDin  = 47;

static int8_t s_dacBclk = 15;
static int8_t s_dacLrc  = 16;
static int8_t s_dacDout = 7;

static int8_t s_uartTxPin = 1;
static int8_t s_uartRxPin = 2;

static BtSlaveCallbacks s_callbacks;
static TaskHandle_t s_btSlaveTaskHandle = nullptr;
static TaskHandle_t s_btUartTaskHandle  = nullptr;
static volatile bool s_btRunning = false;
static volatile bool s_btStreaming = false;
static volatile bool s_btSuspended = false;
static volatile bool s_slaveInstalled = false;
static volatile bool s_dacInstalled = false;
static volatile uint8_t s_btVolume = 100;
static volatile bool s_waitingBclkStop = false;
static volatile uint32_t s_waitingBclkStopStartMs = 0;
static BtSlavePins s_btSlavePins;

// UART Serial kết nối với ESP32 thường
static HardwareSerial SerialBt(1);
static BtSlaveInfo s_btInfo;
static portMUX_TYPE s_btInfoMux = portMUX_INITIALIZER_UNLOCKED;

static char s_btConfigName[48] = "VBot Client Socket";
static bool s_btConfigEnabled = true;

void vbot_bt_slave_set_pins(int8_t bclk, int8_t ws, int8_t din) {
  s_slaveBclk = bclk;
  s_slaveWs   = ws;
  s_slaveDin  = din;
}

void vbot_bt_slave_set_dac_pins(int8_t bclk, int8_t lrc, int8_t dout) {
  s_dacBclk = bclk;
  s_dacLrc  = lrc;
  s_dacDout = dout;
}

void vbot_bt_slave_set_uart_pins(int8_t txPin, int8_t rxPin) {
  s_uartTxPin = txPin;
  s_uartRxPin = rxPin;
}

void vbot_bt_slave_set_volume(uint8_t vol) {
  if (vol > 100) vol = 100;
  s_btVolume = vol;
}

uint8_t vbot_bt_slave_get_volume() {
  return s_btVolume;
}

// Kiểm tra xem chân BCLK từ ESP32 thường có đang phát xung dao động I2S (tần số ~1.4MHz) hay không
static bool isBclkActive() {
  if (s_slaveBclk < 0) return false;
  pinMode(s_slaveBclk, INPUT_PULLDOWN);
  int last = digitalRead(s_slaveBclk);
  int transitions = 0;
  uint32_t tStart = micros();
  // Đo trong 60 microsecond: Nếu là I2S BCLK 1.4MHz (~0.7us/chu kỳ) sẽ có khoảng 85 chu kỳ = ~170 transitions.
  // Nếu là nhiễu ngẫu nhiên hoặc xung ngắn thì transitions thường < 10.
  while ((micros() - tStart) < 60) {
    int cur = digitalRead(s_slaveBclk);
    if (cur != last) {
      transitions++;
      last = cur;
    }
  }
  return (transitions >= 25);
}

static bool installSlaveI2s() {
  if (s_slaveInstalled) return true;

  i2s_config_t slaveConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_SLAVE | I2S_MODE_RX),
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t slavePins = {
    .bck_io_num = s_slaveBclk,
    .ws_io_num = s_slaveWs,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = s_slaveDin
  };

  esp_err_t err = i2s_driver_install(BT_SLAVE_I2S_PORT, &slaveConfig, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("[BT-Slave] Loi cai dat I2S Slave RX: %d\n", err);
    return false;
  }
  err = i2s_set_pin(BT_SLAVE_I2S_PORT, &slavePins);
  if (err != ESP_OK) {
    Serial.printf("[BT-Slave] Loi cai dat chan I2S Slave RX: %d\n", err);
    i2s_driver_uninstall(BT_SLAVE_I2S_PORT);
    return false;
  }

  s_slaveInstalled = true;
  Serial.printf("[BT-Slave] >> Da cai dat I2S Slave RX: BCLK=GPIO%d, WS=GPIO%d, DIN=GPIO%d\n",
                s_slaveBclk, s_slaveWs, s_slaveDin);
  return true;
}

static void uninstallSlaveI2s() {
  if (!s_slaveInstalled) return;
  i2s_zero_dma_buffer(BT_SLAVE_I2S_PORT);
  i2s_driver_uninstall(BT_SLAVE_I2S_PORT);
  s_slaveInstalled = false;
  Serial.println("[BT-Slave] Da go bo I2S Slave RX.");
}

// Chuyển cấu hình xung clock của DAC Loa sang 44.1kHz Stereo
static bool prepareDacForBluetooth() {
  esp_err_t err = i2s_set_clk(BT_DAC_I2S_PORT, 44100, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
  if (err != ESP_OK) {
    Serial.printf("[BT-Slave] Khong the set clock DAC sang 44.1kHz Stereo: %d\n", err);
    return false;
  }
  s_dacInstalled = true;
  Serial.printf("[BT-Slave] >> Da chuyen DAC Loa sang 44.1kHz Stereo.\n");
  return true;
}

// Trả lại cấu hình xung clock của DAC Loa về 16kHz Mono cho VBot
static void restoreDacForVBot() {
  if (!s_dacInstalled) return;
  i2s_zero_dma_buffer(BT_DAC_I2S_PORT);
  esp_err_t err = i2s_set_clk(BT_DAC_I2S_PORT, 16000, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
  if (err != ESP_OK) {
    Serial.printf("[BT-Slave] Khong the set clock DAC ve 16kHz Mono: %d\n", err);
  } else {
    Serial.println("[BT-Slave] >> Da tra lai DAC Loa ve 16kHz Mono cho VBot.");
  }
  s_dacInstalled = false;
}

void vbot_bt_slave_suspend() {
  s_btSuspended = true;
  uint32_t t0 = millis();
  while ((s_btStreaming || s_slaveInstalled) && (millis() - t0 < 300)) {
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void vbot_bt_slave_resume() {
  s_btSuspended = false;
}

// Xử lý các dòng text nhận từ ESP32 thường qua UART
static void handleUartLine(const String &line) {
  if (line == "REQ:CONFIG") {
    Serial.println("[BT-UART] ESP32 thuong yeu cau dong bo cau hinh (REQ:CONFIG)...");
    vbot_bt_slave_sync_config(s_btConfigName, s_btConfigEnabled);
  }
  else if (line.startsWith("META:TITLE:")) {
    String val = line.substring(11);
    portENTER_CRITICAL(&s_btInfoMux);
    strlcpy(s_btInfo.title, val.c_str(), sizeof(s_btInfo.title));
    s_btInfo.connected = true;
    portEXIT_CRITICAL(&s_btInfoMux);
    Serial.printf("[BT-UART] >> Bai hat: %s\n", val.c_str());
    if (s_callbacks.onMeta) s_callbacks.onMeta(s_btInfo.title, s_btInfo.artist);
  }
  else if (line.startsWith("META:ARTIST:")) {
    String val = line.substring(12);
    portENTER_CRITICAL(&s_btInfoMux);
    strlcpy(s_btInfo.artist, val.c_str(), sizeof(s_btInfo.artist));
    s_btInfo.connected = true;
    portEXIT_CRITICAL(&s_btInfoMux);
    Serial.printf("[BT-UART] >> Ca si: %s\n", val.c_str());
    if (s_callbacks.onMeta) s_callbacks.onMeta(s_btInfo.title, s_btInfo.artist);
  }
  else if (line.startsWith("META:ALBUM:")) {
    String val = line.substring(11);
    portENTER_CRITICAL(&s_btInfoMux);
    strlcpy(s_btInfo.album, val.c_str(), sizeof(s_btInfo.album));
    portEXIT_CRITICAL(&s_btInfoMux);
  }
  else if (line.startsWith("META:PEER:")) {
    String val = line.substring(10);
    portENTER_CRITICAL(&s_btInfoMux);
    strlcpy(s_btInfo.peerDevice, val.c_str(), sizeof(s_btInfo.peerDevice));
    if (val.length() > 0) s_btInfo.connected = true;
    portEXIT_CRITICAL(&s_btInfoMux);
    Serial.printf("[BT-UART] >> Thiet bi ket noi (Peer): %s\n", val.c_str());
    if (s_callbacks.onMeta) s_callbacks.onMeta(s_btInfo.title, s_btInfo.artist);
  }
  else if (line == "STATUS:PLAYING") {
    portENTER_CRITICAL(&s_btInfoMux);
    s_btInfo.connected = true;
    strlcpy(s_btInfo.status, "PLAYING", sizeof(s_btInfo.status));
    portEXIT_CRITICAL(&s_btInfoMux);
    s_waitingBclkStop = false;
    s_btSuspended = false;
    Serial.println("[BT-UART] >> Trang thai: PLAYING");
    if (s_callbacks.onStatus) s_callbacks.onStatus("PLAYING");
  }
  else if (line == "STATUS:PAUSED") {
    portENTER_CRITICAL(&s_btInfoMux);
    strlcpy(s_btInfo.status, "PAUSED", sizeof(s_btInfo.status));
    portEXIT_CRITICAL(&s_btInfoMux);
    s_waitingBclkStop = true;
    s_waitingBclkStopStartMs = millis();
    s_btSuspended = true; // Yeu cau task BT-Slave tu dong tra Mic an toan, tranh xung dot I2S
    Serial.println("[BT-UART] >> Trang thai: PAUSED");
    if (s_callbacks.onStatus) s_callbacks.onStatus("PAUSED");
  }
  else if (line == "STATUS:STOPPED") {
    portENTER_CRITICAL(&s_btInfoMux);
    strlcpy(s_btInfo.status, "STOPPED", sizeof(s_btInfo.status));
    portEXIT_CRITICAL(&s_btInfoMux);
    s_waitingBclkStop = true;
    s_waitingBclkStopStartMs = millis();
    s_btSuspended = true; // Yeu cau task BT-Slave tu dong tra Mic an toan
    Serial.println("[BT-UART] >> Trang thai: STOPPED");
    if (s_callbacks.onStatus) s_callbacks.onStatus("STOPPED");
  }
  else if (line == "STATUS:CONNECTED") {
    portENTER_CRITICAL(&s_btInfoMux);
    s_btInfo.connected = true;
    strlcpy(s_btInfo.status, "CONNECTED", sizeof(s_btInfo.status));
    portEXIT_CRITICAL(&s_btInfoMux);
    Serial.println("[BT-UART] >> Thiet bi Bluetooth da KET NOI.");
    if (s_callbacks.onStatus) s_callbacks.onStatus("CONNECTED");
  }
  else if (line == "STATUS:DISCONNECTED") {
    portENTER_CRITICAL(&s_btInfoMux);
    s_btInfo.connected = false;
    strlcpy(s_btInfo.status, "DISCONNECTED", sizeof(s_btInfo.status));
    s_btInfo.title[0] = '\0';
    s_btInfo.artist[0] = '\0';
    s_btInfo.album[0] = '\0';
    s_btInfo.peerDevice[0] = '\0';
    portEXIT_CRITICAL(&s_btInfoMux);
    s_waitingBclkStop = true;
    s_waitingBclkStopStartMs = millis();
    s_btSuspended = true; // Yeu cau task BT-Slave tu dong tra Mic an toan
    Serial.println("[BT-UART] >> Thiet bi Bluetooth da NGAT KET NOI.");
    if (s_callbacks.onMeta) s_callbacks.onMeta("", "");
    if (s_callbacks.onStatus) s_callbacks.onStatus("DISCONNECTED");
  }
  else if (line.startsWith("PINS:")) {
    String params = line.substring(5);
    int start = 0;
    while (start < params.length()) {
      int comma = params.indexOf(',', start);
      if (comma == -1) comma = params.length();
      String pair = params.substring(start, comma);
      pair.trim();
      int eq = pair.indexOf('=');
      if (eq > 0) {
        String k = pair.substring(0, eq);
        int v = pair.substring(eq + 1).toInt();
        k.toUpperCase();
        if (k == "BCLK") s_btSlavePins.bclk = v;
        else if (k == "WS") s_btSlavePins.ws = v;
        else if (k == "DOUT") s_btSlavePins.dout = v;
        else if (k == "RX") s_btSlavePins.rx = v;
        else if (k == "TX") s_btSlavePins.tx = v;
        else if (k == "BTN1") s_btSlavePins.btn1 = v;
        else if (k == "BTN2") s_btSlavePins.btn2 = v;
        else if (k == "BTN3") s_btSlavePins.btn3 = v;
        else if (k == "BTN4") s_btSlavePins.btn4 = v;
      }
      start = comma + 1;
    }
    s_btSlavePins.loaded = true;
    Serial.printf("[BT-UART] >> Da nhan cau hinh GPIO tu ESP32 thuong: BCLK=%d, WS=%d, DOUT=%d, RX=%d, TX=%d, BTN1=%d, BTN2=%d, BTN3=%d, BTN4=%d\n",
                  s_btSlavePins.bclk, s_btSlavePins.ws, s_btSlavePins.dout, s_btSlavePins.rx, s_btSlavePins.tx,
                  s_btSlavePins.btn1, s_btSlavePins.btn2, s_btSlavePins.btn3, s_btSlavePins.btn4);
  }
}

// Task nền đọc dữ liệu UART từ ESP32 thường
static void vbot_bt_uart_task(void *param) {
  String rxBuffer = "";
  rxBuffer.reserve(256);

  Serial.println("[BT-UART] Task doc UART bat dau (Core 1).");
  vTaskDelay(pdMS_TO_TICKS(1500));
  vbot_bt_slave_sync_config(s_btConfigName, s_btConfigEnabled);

  while (s_btRunning) {
    while (SerialBt.available()) {
      char c = (char)SerialBt.read();
      if (c == '\n') {
        rxBuffer.trim();
        if (rxBuffer.length() > 0) {
          handleUartLine(rxBuffer);
        }
        rxBuffer = "";
      } else if (c != '\r') {
        if (rxBuffer.length() < 250) {
          rxBuffer += c;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  vTaskDelete(NULL);
}

static void vbot_bt_slave_task(void *param) {
  constexpr size_t BUF_SAMPLES = 256;
  int16_t pcmBuffer[BUF_SAMPLES * 2]; // Stereo 16-bit
  size_t bytesRead = 0;
  size_t bytesWritten = 0;
  uint32_t lastI2sDataMs = 0;

  Serial.println("[BT-Slave] Task giam sat nhac Bluetooth bat dau hoat dong.");

  while (s_btRunning) {
    // 1. Kiểm tra xem VBot có đang bận (thu âm, xử lý, phát ding/TTS) hoặc bị suspend không
    bool assistantBusy = (s_callbacks.isVBotBusy && s_callbacks.isVBotBusy());
    if (assistantBusy) {
      s_btSuspended = true;
    }

    if (s_btSuspended) {
      if (s_btStreaming || s_slaveInstalled) {
        Serial.println("\n[BT-Slave] >>> Tam dung nhac Bluetooth. Tra lai Mic va WakeNet ngay!");
        uninstallSlaveI2s();
        restoreDacForVBot();
        s_btStreaming = false;
        if (s_callbacks.onStop) s_callbacks.onStop();
        if (s_callbacks.onResumeMic) s_callbacks.onResumeMic();
      }
      // Kiểm tra xem ESP32 thường có đang ở trạng thái ngừng phát hay không
      bool uartSuspended = false;
      portENTER_CRITICAL(&s_btInfoMux);
      if (strcmp(s_btInfo.status, "PAUSED") == 0 ||
          strcmp(s_btInfo.status, "STOPPED") == 0 ||
          strcmp(s_btInfo.status, "DISCONNECTED") == 0) {
        uartSuspended = true;
      }
      portEXIT_CRITICAL(&s_btInfoMux);

      // Chỉ hủy cờ suspend khi VBot rảnh rỗi VÀ UART không bị tạm dừng
      if (!assistantBusy && !uartSuspended) {
        s_btSuspended = false;
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    if (!s_btStreaming) {
      // 1. Kiểm tra trạng thái báo từ UART của ESP32 thường
      bool uartPlaying = false;
      bool uartSuspended = false;
      portENTER_CRITICAL(&s_btInfoMux);
      if (strcmp(s_btInfo.status, "PLAYING") == 0) {
        uartPlaying = true;
      } else if (strcmp(s_btInfo.status, "PAUSED") == 0 ||
                 strcmp(s_btInfo.status, "STOPPED") == 0 ||
                 strcmp(s_btInfo.status, "DISCONNECTED") == 0) {
        uartSuspended = true;
      }
      portEXIT_CRITICAL(&s_btInfoMux);

      // Nếu ESP32 thường báo trạng thái rõ ràng là PAUSED/STOPPED/DISCONNECTED:
      // Chắc chắn không có phát nhạc -> Bỏ qua mọi xung nhiễu và tiếp tục ngủ.
      if (uartSuspended) {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }

      // Nếu vừa Pause xong, cho timeout 800ms để xung clock và đệm ổn định hoàn toàn
      if (s_waitingBclkStop) {
        if (!isBclkActive() || (millis() - s_waitingBclkStopStartMs > 800)) {
          s_waitingBclkStop = false;
          Serial.println("[BT-Slave] Da san sang nhan nhac tiep.");
        } else {
          vTaskDelay(pdMS_TO_TICKS(80));
          continue;
        }
      }

      // TRẠNG THÁI CHỜ: VBot đang dùng Mic INMP441 để nhận diện WakeNet
      // Chỉ nhận nhạc khi UART báo PLAYING hoặc BCLK dao động chuẩn I2S (tần số cao)
      if (uartPlaying || isBclkActive()) {
        Serial.println("\n[BT-Slave] >>> Phat hien xung nhac Bluetooth tu ESP32 thuong!");
        s_btStreaming = true;

        // Tạm dừng Mic INMP441 để giải phóng cổng phần cứng I2S_NUM_1
        if (s_callbacks.onSuspendMic) s_callbacks.onSuspendMic();

        // Tạm dừng phát âm thanh nội bộ của VBot
        if (s_callbacks.onPlay) s_callbacks.onPlay();

        // Cài đặt I2S Slave RX trên I2S_NUM_1
        installSlaveI2s();

        // Chuyển DAC Loa MAX98357 sang 44.1kHz Stereo
        prepareDacForBluetooth();

        lastI2sDataMs = millis();
      } else {
        // Chưa có nhạc: Ngủ 100ms, tiêu thụ 0% CPU để Mic và WakeNet hoạt động tối đa
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
    }

    // Kiểm tra nhanh trước khi đọc I2S
    if (s_btSuspended || (s_callbacks.isVBotBusy && s_callbacks.isVBotBusy())) {
      s_btSuspended = true;
      continue;
    }

    // TRẠNG THÁI STREAMING: Đang nhận nhạc từ ESP32 thường và phát ra Loa
    esp_err_t res = i2s_read(BT_SLAVE_I2S_PORT, pcmBuffer, sizeof(pcmBuffer), &bytesRead, pdMS_TO_TICKS(40));

    // Nếu vừa đọc I2S xong mà VBot bị đánh thức, lập tức chuyển nhượng
    if (s_btSuspended || (s_callbacks.isVBotBusy && s_callbacks.isVBotBusy())) {
      s_btSuspended = true;
      continue;
    }

    if (res == ESP_OK && bytesRead > 0) {
      int sampleCount = bytesRead / sizeof(int16_t);

      // Áp dụng âm lượng phần cứng từ phím bấm VBot
      uint8_t vol = s_btVolume;
      int32_t volFactor = (int32_t)vol;
      uint16_t outPeak = 0;

      for (int i = 0; i < sampleCount; i++) {
        int32_t s = (pcmBuffer[i] * volFactor) / 100;
        pcmBuffer[i] = (int16_t)constrain(s, -32768, 32767);
        int16_t absVal = abs(pcmBuffer[i]);
        if (absVal > outPeak) outPeak = absVal;
      }

      // Ghi ra Loa MAX98357
      i2s_write(BT_DAC_I2S_PORT, pcmBuffer, bytesRead, &bytesWritten, portMAX_DELAY);

      // Nháy LED hiệu ứng theo nhạc
      if (s_callbacks.onPeak) {
        s_callbacks.onPeak(outPeak);
      }

      // Cập nhật mốc thời gian nhận dữ liệu I2S từ ESP32 thường
      lastI2sDataMs = millis();
    } else {
      // Đọc timeout hoặc không có dữ liệu (> 1000ms không có xung I2S từ ESP32 thường do đã tạm dừng/ngắt kết nối)
      if (millis() - lastI2sDataMs > 1000) {
        Serial.println("\n[BT-Slave] >>> Xung I2S Bluetooth da ngat (> 1000ms). Dang khoi phuc lai Microphone va VBot WakeNet...");
        s_btStreaming = false;
        s_waitingBclkStop = true;
        s_waitingBclkStopStartMs = millis();

        uninstallSlaveI2s();
        restoreDacForVBot();

        if (s_callbacks.onStop) s_callbacks.onStop();
        if (s_callbacks.onResumeMic) s_callbacks.onResumeMic();

        Serial.println("[BT-Slave] >>> Da khoi phuc VBot thanh cong. San sang nhan lenh 'VBot oi'!");
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }

  if (s_btStreaming) {
    s_btStreaming = false;
    uninstallSlaveI2s();
    restoreDacForVBot();
    if (s_callbacks.onStop) s_callbacks.onStop();
    if (s_callbacks.onResumeMic) s_callbacks.onResumeMic();
  }

  s_waitingBclkStop = false;
  s_btRunning = false;
  vTaskDelete(NULL);
}

bool vbot_bt_slave_init(const BtSlaveCallbacks &callbacks) {
  if (s_btRunning) return true;

  s_callbacks = callbacks;
  s_btRunning = true;
  s_btStreaming = false;
  s_btSuspended = false;
  s_waitingBclkStop = false;

  // Khởi tạo UART kết nối ESP32 thường
  if (s_uartTxPin >= 0 && s_uartRxPin >= 0) {
    SerialBt.begin(115200, SERIAL_8N1, s_uartRxPin, s_uartTxPin);
    Serial.printf("[BT-UART] Khoi tao cong UART1: TX=GPIO%d, RX=GPIO%d (115200 baud)\n",
                  s_uartTxPin, s_uartRxPin);
    xTaskCreatePinnedToCore(
      vbot_bt_uart_task,
      "bt_uart",
      4096,
      nullptr,
      2,
      &s_btUartTaskHandle,
      1
    );
  }

  // Khởi tạo task giám sát I2S Slave
  xTaskCreatePinnedToCore(
    vbot_bt_slave_task,
    "bt_slave",
    4096,
    nullptr,
    2,
    &s_btSlaveTaskHandle,
    1
  );

  Serial.println("[BT-Slave] Khoi tao module nhan Bluetooth I2S Slave thanh cong!");
  return true;
}

void vbot_bt_slave_stop() {
  if (!s_btRunning) return;

  s_btRunning = false;
  if (s_btUartTaskHandle) {
    vTaskDelete(s_btUartTaskHandle);
    s_btUartTaskHandle = nullptr;
  }
  if (s_btSlaveTaskHandle) {
    vTaskDelete(s_btSlaveTaskHandle);
    s_btSlaveTaskHandle = nullptr;
  }
  uninstallSlaveI2s();
  restoreDacForVBot();
}

bool vbot_bt_slave_is_streaming() {
  return s_btStreaming;
}

void vbot_bt_slave_sync_config(const char *name, bool enabled) {
  if (name && name[0]) {
    strlcpy(s_btConfigName, name, sizeof(s_btConfigName));
  }
  s_btConfigEnabled = enabled;

  portENTER_CRITICAL(&s_btInfoMux);
  s_btInfo.enabled = enabled;
  strlcpy(s_btInfo.btName, s_btConfigName, sizeof(s_btInfo.btName));
  portEXIT_CRITICAL(&s_btInfoMux);

  if (s_uartTxPin >= 0) {
    SerialBt.printf("CFG:NAME:%s\n", s_btConfigName);
    SerialBt.printf("CFG:ENABLE:%d\n", enabled ? 1 : 0);
    Serial.printf("[BT-UART] Da gui cau hinh sang ESP32 thuong: NAME='%s', ENABLE=%d (TX=GPIO%d, RX=GPIO%d)\n",
                  s_btConfigName, enabled ? 1 : 0, s_uartTxPin, s_uartRxPin);
  }
}

void vbot_bt_slave_send_cmd(const char *cmd) {
  if (!cmd || !cmd[0] || s_uartTxPin < 0) return;
  String c = String(cmd);
  c.trim();
  if (c.equalsIgnoreCase("PLAY") || c.equalsIgnoreCase("CMD:PLAY")) {
    s_waitingBclkStop = false;
  }
  if (!c.startsWith("CMD:")) {
    c = "CMD:" + c;
  }
  SerialBt.println(c);
  Serial.printf("[BT-UART] Da gui lenh sang ESP32 thuong: %s\n", c.c_str());
}

BtSlaveInfo vbot_bt_slave_get_info() {
  BtSlaveInfo copy;
  portENTER_CRITICAL(&s_btInfoMux);
  copy = s_btInfo;
  portEXIT_CRITICAL(&s_btInfoMux);
  return copy;
}

BtSlavePins vbot_bt_slave_get_pins() {
  return s_btSlavePins;
}

void vbot_bt_slave_request_pins() {
  vbot_bt_slave_send_cmd("REQ:PINS");
}

void vbot_bt_slave_set_slave_pins(const BtSlavePins &pins) {
  s_btSlavePins = pins;
  s_btSlavePins.loaded = true;
  char buf[128];
  snprintf(buf, sizeof(buf), "CMD:SET_PINS:BCLK=%d,WS=%d,DOUT=%d,RX=%d,TX=%d,BTN1=%d,BTN2=%d,BTN3=%d,BTN4=%d",
           pins.bclk, pins.ws, pins.dout, pins.rx, pins.tx,
           pins.btn1, pins.btn2, pins.btn3, pins.btn4);
  vbot_bt_slave_send_cmd(buf);
}

void vbot_bt_slave_reset_slave_pins() {
  s_btSlavePins = BtSlavePins();
  s_btSlavePins.loaded = true;
  vbot_bt_slave_send_cmd("CMD:RESET_PINS");
}
