#include <Arduino.h>
#include <Preferences.h>
#include "BluetoothA2DPSink.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "ButtonHandler.h"

// Chân mặc định ban đầu:
#define DEF_I2S_BCLK 26 // Nối sang GPIO 14 của ESP32-S3
#define DEF_I2S_LRCK 25 // Nối sang GPIO 21 của ESP32-S3
#define DEF_I2S_DOUT 22 // Nối sang GPIO 47 của ESP32-S3
#define DEF_UART_RXD 16 // Nối sang GPIO 1 (TX) của ESP32-S3
#define DEF_UART_TXD 17 // Nối sang GPIO 2 (RX) của ESP32-S3
#define DEF_BTN1_PIN 32 // Nút 1: Play/Pause, Disconnect
#define DEF_BTN2_PIN 33 // Nút 2: Next, Replay, Vol+
#define DEF_BTN3_PIN 27 // Nút 3: Prev, Replay, Vol-
#define DEF_BTN4_PIN 13 // Nút 4: Sync S3, Restart ESP32

// Biến lưu cấu hình chân động (đọc từ NVS flash)
int i2s_bclk_pin = DEF_I2S_BCLK;
int i2s_lrck_pin = DEF_I2S_LRCK;
int i2s_dout_pin = DEF_I2S_DOUT;
int uart_rxd_pin = DEF_UART_RXD;
int uart_txd_pin = DEF_UART_TXD;
int btn1_pin     = DEF_BTN1_PIN;
int btn2_pin     = DEF_BTN2_PIN;
int btn3_pin     = DEF_BTN3_PIN;
int btn4_pin     = DEF_BTN4_PIN;

static Preferences pinPrefs;

void loadPinConfig() {
  pinPrefs.begin("bt_pins", true);
  i2s_bclk_pin = pinPrefs.getInt("bclk", DEF_I2S_BCLK);
  i2s_lrck_pin = pinPrefs.getInt("ws",   DEF_I2S_LRCK);
  i2s_dout_pin = pinPrefs.getInt("dout", DEF_I2S_DOUT);
  uart_rxd_pin = pinPrefs.getInt("rx",   DEF_UART_RXD);
  uart_txd_pin = pinPrefs.getInt("tx",   DEF_UART_TXD);
  btn1_pin     = pinPrefs.getInt("btn1", DEF_BTN1_PIN);
  btn2_pin     = pinPrefs.getInt("btn2", DEF_BTN2_PIN);
  btn3_pin     = pinPrefs.getInt("btn3", DEF_BTN3_PIN);
  btn4_pin     = pinPrefs.getInt("btn4", DEF_BTN4_PIN);
  pinPrefs.end();
}

void savePinConfig(int bclk, int ws, int dout, int rx, int tx, int b1, int b2, int b3, int b4) {
  pinPrefs.begin("bt_pins", false);
  pinPrefs.putInt("bclk", bclk);
  pinPrefs.putInt("ws",   ws);
  pinPrefs.putInt("dout", dout);
  pinPrefs.putInt("rx",   rx);
  pinPrefs.putInt("tx",   tx);
  pinPrefs.putInt("btn1", b1);
  pinPrefs.putInt("btn2", b2);
  pinPrefs.putInt("btn3", b3);
  pinPrefs.putInt("btn4", b4);
  pinPrefs.end();
}

void sendPinsToS3() {
  Serial2.printf("PINS:BCLK=%d,WS=%d,DOUT=%d,RX=%d,TX=%d,BTN1=%d,BTN2=%d,BTN3=%d,BTN4=%d\n",
    i2s_bclk_pin, i2s_lrck_pin, i2s_dout_pin, uart_rxd_pin, uart_txd_pin,
    btn1_pin, btn2_pin, btn3_pin, btn4_pin);
}

static SimpleButton btn1(DEF_BTN1_PIN);
static SimpleButton btn2(DEF_BTN2_PIN);
static SimpleButton btn3(DEF_BTN3_PIN);
static SimpleButton btn4(DEF_BTN4_PIN, true, 45, 320, 2000);

void setI2sPinsIdle() {
  pinMode(i2s_bclk_pin, OUTPUT);
  digitalWrite(i2s_bclk_pin, LOW);
  pinMode(i2s_lrck_pin, OUTPUT);
  digitalWrite(i2s_lrck_pin, LOW);
  pinMode(i2s_dout_pin, OUTPUT);
  digitalWrite(i2s_dout_pin, LOW);
}

void restoreI2sPins() {
  i2s_pin_config_t my_pin_config = {
    .bck_io_num = i2s_bclk_pin,
    .ws_io_num = i2s_lrck_pin,
    .data_out_num = i2s_dout_pin,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_set_pin((i2s_port_t)0, &my_pin_config);
}

class MyA2DPSink : public BluetoothA2DPSink {
public:
  void setI2sActive(bool active) {
    if (active) {
      restoreI2sPins();
      set_i2s_active(true);
    } else {
      set_i2s_active(false);
      setI2sPinsIdle();
    }
  }
};

MyA2DPSink a2dp_sink;

static bool bt_running = false;
static String bt_name = "";
static bool bt_enabled_by_s3 = true;
static uint32_t last_req_ms = 0;
static uint32_t last_peer_poll_ms = 0;
static bool config_received = false;

static String last_peer_name = "";
static String last_title = "";
static String last_artist = "";
static String last_album = "";
static esp_a2d_audio_state_t last_audio_state = ESP_A2D_AUDIO_STATE_STOPPED;
static esp_a2d_connection_state_t last_conn_state = ESP_A2D_CONNECTION_STATE_DISCONNECTED;

// Tên Bluetooth mặc định: VBot Client {Tên Chip} {Mã Chip}
String getDefaultBtName() {
  uint64_t chipid = ESP.getEfuseMac();
  uint16_t chip_suffix = (uint16_t)(chipid >> 32);
  char defaultName[40];
  snprintf(defaultName, sizeof(defaultName), "VBot Client ESP32 %04X", chip_suffix);
  return String(defaultName);
}

void sendFullStatusToS3() {
  if (a2dp_sink.is_connected()) {
    if (last_audio_state == ESP_A2D_AUDIO_STATE_STARTED) {
      Serial2.println("STATUS:PLAYING");
    } else if (last_audio_state == ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND) {
      Serial2.println("STATUS:PAUSED");
    } else {
      Serial2.println("STATUS:CONNECTED");
    }

    const char *p = a2dp_sink.get_peer_name();
    if (p && strlen(p) > 0 && strcmp(p, "unknown") != 0) {
      last_peer_name = String(p);
    }
    if (last_peer_name.length() > 0) {
      Serial2.printf("META:PEER:%s\n", last_peer_name.c_str());
    } else {
      esp_bt_gap_read_remote_name(*a2dp_sink.get_current_peer_address());
    }

    if (last_title.length() > 0) Serial2.printf("META:TITLE:%s\n", last_title.c_str());
    if (last_artist.length() > 0) Serial2.printf("META:ARTIST:%s\n", last_artist.c_str());
    if (last_album.length() > 0) Serial2.printf("META:ALBUM:%s\n", last_album.c_str());
  } else {
    Serial2.println("STATUS:DISCONNECTED");
    Serial2.println("META:PEER:");
  }
}

void avrc_metadata_callback(uint8_t data1, const uint8_t *data2) {
  if (!data2) return;
  Serial.printf("[A2DP Metadata] %d: %s\n", data1, (const char*)data2);

  if (data1 == 1) { // Title
    last_title = String((const char*)data2);
    Serial2.printf("META:TITLE:%s\n", (const char*)data2);
  } else if (data1 == 2) { // Artist
    last_artist = String((const char*)data2);
    Serial2.printf("META:ARTIST:%s\n", (const char*)data2);
  } else if (data1 == 4) { // Album
    last_album = String((const char*)data2);
    Serial2.printf("META:ALBUM:%s\n", (const char*)data2);
  }
}

void peer_name_callback(char* peer_name) {
  if (peer_name && strlen(peer_name) > 0 && strcmp(peer_name, "unknown") != 0) {
    last_peer_name = String(peer_name);
    Serial.printf("[A2DP] >> Thiet bi ket noi (Peer Name): %s\n", peer_name);
    Serial2.printf("META:PEER:%s\n", peer_name);
  }
}

void avrc_playstatus_callback(esp_avrc_playback_stat_t playback) {
  Serial.printf("[AVRC PlayStatus] %d\n", playback);
  if (playback == ESP_AVRC_PLAYBACK_PLAYING) {
    a2dp_sink.setI2sActive(true);
    last_audio_state = ESP_A2D_AUDIO_STATE_STARTED;
    Serial2.println("STATUS:PLAYING");
  } else if (playback == ESP_AVRC_PLAYBACK_PAUSED) {
    a2dp_sink.setI2sActive(false);
    last_audio_state = ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND;
    Serial2.println("STATUS:PAUSED");
  } else if (playback == ESP_AVRC_PLAYBACK_STOPPED) {
    a2dp_sink.setI2sActive(false);
    last_audio_state = ESP_A2D_AUDIO_STATE_STOPPED;
    Serial2.println("STATUS:STOPPED");
  }
}

void connection_state_changed(esp_a2d_connection_state_t state, void *ptr) {
  last_conn_state = state;
  if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
    Serial.println("[A2DP] >> Da ket noi Bluetooth thanh cong!");
    setI2sPinsIdle();
    Serial2.println("STATUS:CONNECTED");
    
    // Yeu cau doc ten thiet bi ngay
    esp_bt_gap_read_remote_name(*a2dp_sink.get_current_peer_address());
    const char *pName = a2dp_sink.get_peer_name();
    if (pName && strlen(pName) > 0 && strcmp(pName, "unknown") != 0) {
      last_peer_name = String(pName);
      Serial2.printf("META:PEER:%s\n", pName);
    }
  } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
    Serial.println("[A2DP] >> Ngat ket noi Bluetooth.");
    last_peer_name = "";
    last_title = "";
    last_artist = "";
    last_album = "";
    a2dp_sink.setI2sActive(false);
    Serial2.println("STATUS:DISCONNECTED");
    Serial2.println("META:PEER:");
  }
}

void audio_state_changed(esp_a2d_audio_state_t state, void *ptr) {
  last_audio_state = state;
  if (state == ESP_A2D_AUDIO_STATE_STARTED) {
    Serial.println("[A2DP] >> Trang thai: DANG PHAT NHAC (STARTED)!");
    a2dp_sink.setI2sActive(true);
    Serial2.println("STATUS:PLAYING");
  } else if (state == ESP_A2D_AUDIO_STATE_STOPPED) {
    Serial.println("[A2DP] >> Trang thai: DA DUNG (STOPPED).");
    a2dp_sink.setI2sActive(false);
    Serial2.println("STATUS:STOPPED");
  } else if (state == ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND) {
    Serial.println("[A2DP] >> Trang thai: TAM DUNG (SUSPEND).");
    a2dp_sink.setI2sActive(false);
    Serial2.println("STATUS:PAUSED");
  }
}

void startBluetooth() {
  if (bt_running) return;

  String nameToUse = (bt_name.length() > 0) ? bt_name : getDefaultBtName();
  Serial.printf("[A2DP] Dang khoi dong Bluetooth: '%s'...\n", nameToUse.c_str());

  i2s_pin_config_t my_pin_config = {
    .bck_io_num = i2s_bclk_pin,
    .ws_io_num = i2s_lrck_pin,
    .data_out_num = i2s_dout_pin,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  a2dp_sink.set_pin_config(my_pin_config);
  a2dp_sink.set_avrc_metadata_callback(avrc_metadata_callback);
  a2dp_sink.set_peer_name_callback(peer_name_callback);
  a2dp_sink.set_avrc_rn_playstatus_callback(avrc_playstatus_callback);
  a2dp_sink.set_on_connection_state_changed(connection_state_changed);
  a2dp_sink.set_on_audio_state_changed(audio_state_changed);

  a2dp_sink.set_output_active_by_state(true);
  a2dp_sink.set_volume(127);

  a2dp_sink.start(nameToUse.c_str());
  a2dp_sink.setI2sActive(false);
  setI2sPinsIdle();

  bt_running = true;
  Serial.printf("[A2DP] Da phat Bluetooth thanh cong voi ten: '%s'\n", nameToUse.c_str());

  // Kiem tra neu da co ket noi
  if (a2dp_sink.is_connected()) {
    sendFullStatusToS3();
  }
}

void stopBluetooth() {
  if (!bt_running) return;
  Serial.println("[A2DP] Dang tat Bluetooth theo lenh tu ESP32-S3...");
  a2dp_sink.end(true);
  bt_running = false;
  last_peer_name = "";
  last_title = "";
  last_artist = "";
  last_album = "";
  Serial2.println("STATUS:DISCONNECTED");
  Serial2.println("META:PEER:");
  Serial.println("[A2DP] Da tat hoan toan Bluetooth!");
}

void handleButtons() {
  // Nút 1 (GPIO 32): Play/Pause (Click), Disconnect (Long Press)
  ButtonEvent e1 = btn1.update();
  if (e1 == BTN_SINGLE_CLICK) {
    if (last_audio_state == ESP_A2D_AUDIO_STATE_STARTED) {
      Serial.println("[BTN1] Nhan nha -> Tam dung (Pause)");
      a2dp_sink.pause();
    } else {
      Serial.println("[BTN1] Nhan nha -> Phat (Play)");
      a2dp_sink.setI2sActive(true);
      a2dp_sink.play();
    }
  } else if (e1 == BTN_LONG_PRESS_START) {
    Serial.println("[BTN1] Nhan giu -> Ngat ket noi Bluetooth (Disconnect)");
    a2dp_sink.disconnect();
    a2dp_sink.setI2sActive(false);
  }

  // Nút 2 (GPIO 33): Next (Click), Phát lại từ đầu (Double Click), Vol Up (Long Press)
  ButtonEvent e2 = btn2.update();
  if (e2 == BTN_SINGLE_CLICK) {
    Serial.println("[BTN2] Nhan nha -> Chuyen bai tiep theo (Next)");
    a2dp_sink.next();
  } else if (e2 == BTN_DOUBLE_CLICK) {
    Serial.println("[BTN2] Nhan dup -> Phat lai tu dau bai hat!");
    a2dp_sink.rewind();
    a2dp_sink.previous();
  } else if (e2 == BTN_LONG_PRESS_START) {
    Serial.println("[BTN2] Nhan giu -> Tang am luong (Volume Up)");
    int v = min(127, a2dp_sink.get_volume() + 15);
    a2dp_sink.set_volume(v);
  }

  // Nút 3 (GPIO 27): Prev (Click), Phát lại từ đầu (Double Click), Vol Down (Long Press)
  ButtonEvent e3 = btn3.update();
  if (e3 == BTN_SINGLE_CLICK) {
    Serial.println("[BTN3] Nhan nha -> Quay lai bai truoc (Previous)");
    a2dp_sink.previous();
  } else if (e3 == BTN_DOUBLE_CLICK) {
    Serial.println("[BTN3] Nhan dup -> Phat lai tu dau bai hat!");
    a2dp_sink.rewind();
    a2dp_sink.previous();
  } else if (e3 == BTN_LONG_PRESS_START) {
    Serial.println("[BTN3] Nhan giu -> Giam am luong (Volume Down)");
    int v = max(0, a2dp_sink.get_volume() - 15);
    a2dp_sink.set_volume(v);
  }

  // Nút 4 (GPIO 13): Sync S3 (Click), Refresh (Double Click), Restart ESP32 (Long Press > 2s)
  ButtonEvent e4 = btn4.update();
  if (e4 == BTN_SINGLE_CLICK) {
    Serial.println("[BTN4] Nhan nha -> Dong bo thong tin sang ESP32-S3");
    sendFullStatusToS3();
  } else if (e4 == BTN_DOUBLE_CLICK) {
    Serial.println("[BTN4] Nhan dup -> Refresh trang thai");
    sendFullStatusToS3();
  } else if (e4 == BTN_LONG_PRESS_START) {
    Serial.println("\n[BTN4] >>> NHAN GIU > 2S: DANG KHOI DONG LAI ESP32 (RESTART)...");
    Serial2.println("STATUS:DISCONNECTED");
    delay(200);
    ESP.restart();
  }
}

void setup() {
  loadPinConfig();
  btn1.setPin(btn1_pin);
  btn2.setPin(btn2_pin);
  btn3.setPin(btn3_pin);
  btn4.setPin(btn4_pin);
  setI2sPinsIdle();
  btn1.begin();
  btn2.begin();
  btn3.begin();
  btn4.begin();
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, uart_rxd_pin, uart_txd_pin);
  delay(800);

  Serial.println("\n================================================");
  Serial.println("  ESP32 Thuong - Bluetooth A2DP & UART Bridge   ");
  Serial.printf ("  I2S: BCLK=GPIO%d, LRCK=GPIO%d, DOUT=GPIO%d\n", i2s_bclk_pin, i2s_lrck_pin, i2s_dout_pin);
  Serial.printf ("  UART: RX2=GPIO%d, TX2=GPIO%d\n", uart_rxd_pin, uart_txd_pin);
  Serial.printf ("  Buttons: BTN1=%d, BTN2=%d, BTN3=%d, BTN4=%d\n", btn1_pin, btn2_pin, btn3_pin, btn4_pin);
  Serial.println("================================================");

  // Gửi cấu hình chân hiện tại và xin cấu hình từ S3
  sendPinsToS3();
  Serial2.println("REQ:CONFIG");
  last_req_ms = millis();
}

void loop() {
  handleButtons();

  // Đọc lệnh từ ESP32-S3 gửi sang
  if (Serial2.available()) {
    String line = Serial2.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      Serial.printf("[UART-IN] %s\n", line.c_str());

      if (line.startsWith("CFG:NAME:")) {
        String newName = line.substring(9);
        newName.trim();
        config_received = true;
        if (newName.length() > 0 && newName != bt_name) {
          bt_name = newName;
          if (bt_running) {
            Serial.println("[A2DP] Ten thiet bi thay doi. Khoi dong lai Bluetooth...");
            stopBluetooth();
            delay(200);
            startBluetooth();
          }
        }
        sendFullStatusToS3();
      }
      else if (line.startsWith("CFG:ENABLE:")) {
        int en = line.substring(11).toInt();
        config_received = true;
        bt_enabled_by_s3 = (en == 1);
        if (bt_enabled_by_s3) {
          startBluetooth();
        } else {
          stopBluetooth();
        }
        sendFullStatusToS3();
      }
      else if (line == "CMD:PLAY") {
        Serial.println("[A2DP] Nhan lenh PLAY tu ESP32-S3");
        a2dp_sink.setI2sActive(true);
        a2dp_sink.play();
        Serial2.println("STATUS:PLAYING");
      }
      else if (line == "CMD:PAUSE") {
        Serial.println("[A2DP] Nhan lenh PAUSE tu ESP32-S3");
        a2dp_sink.pause();
        a2dp_sink.setI2sActive(false);
        Serial2.println("STATUS:PAUSED");
      }
      else if (line == "CMD:NEXT") {
        Serial.println("[A2DP] Nhan lenh NEXT tu ESP32-S3");
        a2dp_sink.next();
      }
      else if (line == "CMD:PREV") {
        Serial.println("[A2DP] Nhan lenh PREV tu ESP32-S3");
        a2dp_sink.previous();
      }
      else if (line == "CMD:DISCONNECT") {
        Serial.println("[A2DP] Nhan lenh DISCONNECT tu ESP32-S3");
        a2dp_sink.disconnect();
        a2dp_sink.setI2sActive(false);
      }
      else if (line == "CMD:STATUS" || line == "REQ:STATUS") {
        sendFullStatusToS3();
      }
      else if (line == "REQ:PINS" || line == "CMD:GET_PINS") {
        sendPinsToS3();
      }
      else if (line.startsWith("CMD:SET_PINS:")) {
        String params = line.substring(13);
        int nb = i2s_bclk_pin, nw = i2s_lrck_pin, nd = i2s_dout_pin;
        int nrx = uart_rxd_pin, ntx = uart_txd_pin;
        int nb1 = btn1_pin, nb2 = btn2_pin, nb3 = btn3_pin, nb4 = btn4_pin;
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
            if (k == "BCLK") nb = v;
            else if (k == "WS") nw = v;
            else if (k == "DOUT") nd = v;
            else if (k == "RX") nrx = v;
            else if (k == "TX") ntx = v;
            else if (k == "BTN1") nb1 = v;
            else if (k == "BTN2") nb2 = v;
            else if (k == "BTN3") nb3 = v;
            else if (k == "BTN4") nb4 = v;
          }
          start = comma + 1;
        }
        savePinConfig(nb, nw, nd, nrx, ntx, nb1, nb2, nb3, nb4);
        Serial2.println("SET_PINS:OK");
        sendPinsToS3();
        Serial.println("[A2DP] Da luu cau hinh GPIO moi vao NVS. Khoi dong lai sau 500ms...");
        delay(500);
        ESP.restart();
      }
      else if (line == "CMD:RESET_PINS") {
        pinPrefs.begin("bt_pins", false);
        pinPrefs.clear();
        pinPrefs.end();
        Serial2.println("RESET_PINS:OK");
        Serial.println("[A2DP] Da khoi phuc GPIO mac dinh. Khoi dong lai sau 500ms...");
        delay(500);
        ESP.restart();
      }
    }
  }

  // Định kỳ thăm dò tên thiết bị nếu đang kết nối mà chưa có tên
  if (a2dp_sink.is_connected()) {
    if (last_peer_name.length() == 0 && (millis() - last_peer_poll_ms > 2000)) {
      last_peer_poll_ms = millis();
      const char *p = a2dp_sink.get_peer_name();
      if (p && strlen(p) > 0 && strcmp(p, "unknown") != 0) {
        last_peer_name = String(p);
        Serial2.printf("META:PEER:%s\n", p);
        Serial.printf("[A2DP] Da lay duoc ten thiet bi qua polling: %s\n", p);
      } else {
        esp_bt_gap_read_remote_name(*a2dp_sink.get_current_peer_address());
      }
    }
  }

  // Nếu sau 1.5 giây chưa nhận được cấu hình từ S3 thì xin lại
  if (!config_received && (millis() - last_req_ms > 1500)) {
    Serial2.println("REQ:CONFIG");
    last_req_ms = millis();
  }

  // Nếu sau 5 giây vẫn chưa nhận được cấu hình (ví dụ S3 chưa bật), tự khởi động với tên mặc định
  if (!config_received && !bt_running && millis() > 5000) {
    Serial.println("[A2DP] Chua nhan duoc cau hinh tu S3 sau 5s, khoi dong voi ten mac dinh...");
    startBluetooth();
  }

  delay(10);
}
