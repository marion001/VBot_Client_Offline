#pragma once

#include <Arduino.h>

typedef void (*BtSlaveSimpleCallback)();
typedef void (*BtSlavePeakCallback)(uint16_t peak);
typedef bool (*BtSlaveBoolCallback)();
typedef void (*BtSlaveMetaCallback)(const char *title, const char *artist);
typedef void (*BtSlaveStatusCallback)(const char *status);

struct BtSlaveCallbacks {
  BtSlaveSimpleCallback onPlay = nullptr;
  BtSlaveSimpleCallback onStop = nullptr;
  BtSlavePeakCallback onPeak = nullptr;
  BtSlaveSimpleCallback onSuspendMic = nullptr;
  BtSlaveSimpleCallback onResumeMic = nullptr;
  BtSlaveBoolCallback isVBotBusy = nullptr;
  BtSlaveMetaCallback onMeta = nullptr;
  BtSlaveStatusCallback onStatus = nullptr;
};

// Cấu trúc thông tin Metadata & trạng thái từ ESP32 thường
struct BtSlaveInfo {
  bool enabled = false;
  bool connected = false;
  char status[16] = "IDLE";
  char title[128] = "";
  char artist[64] = "";
  char album[64] = "";
  char btName[48] = "";
  char peerDevice[64] = "";
};

// Cấu trúc chân GPIO của ESP32 thường
struct BtSlavePins {
  int8_t bclk = 26;
  int8_t ws = 25;
  int8_t dout = 22;
  int8_t rx = 16;
  int8_t tx = 17;
  int8_t btn1 = 32;
  int8_t btn2 = 33;
  int8_t btn3 = 27;
  int8_t btn4 = 13;
  bool loaded = false;
};

// Cấu hình chân nhận I2S Slave RX từ ESP32 thường
void vbot_bt_slave_set_pins(int8_t bclk, int8_t ws, int8_t din);

// Cấu hình chân xuất I2S DAC Loa MAX98357
void vbot_bt_slave_set_dac_pins(int8_t bclk, int8_t lrc, int8_t dout);

// Cấu hình chân UART giao tiếp với ESP32 thường (Mặc định TX=GPIO1, RX=GPIO2)
void vbot_bt_slave_set_uart_pins(int8_t txPin, int8_t rxPin);

// Khởi tạo luồng nhận I2S Slave RX và UART
bool vbot_bt_slave_init(const BtSlaveCallbacks &callbacks);

// Dừng luồng I2S Slave và UART
void vbot_bt_slave_stop();

// Tạm dừng ngay lập tức luồng Bluetooth khi VBot cần thức dậy
void vbot_bt_slave_suspend();

// Cho phép tiếp tục luồng Bluetooth khi VBot về trạng thái rảnh
void vbot_bt_slave_resume();

// Kiểm tra xem luồng Bluetooth có đang phát nhạc ra loa không
bool vbot_bt_slave_is_streaming();

// Đặt âm lượng (0 - 100)
void vbot_bt_slave_set_volume(uint8_t vol);

// Lấy âm lượng hiện tại
uint8_t vbot_bt_slave_get_volume();

// Gửi lệnh điều khiển qua UART sang ESP32 thường (PLAY, PAUSE, NEXT, PREV, DISCONNECT)
void vbot_bt_slave_send_cmd(const char *cmd);

// Đồng bộ tên thiết bị và trạng thái kích hoạt Bluetooth sang ESP32 thường qua UART
void vbot_bt_slave_sync_config(const char *deviceName, bool enabled);

// Lấy thông tin Metadata hiện tại
BtSlaveInfo vbot_bt_slave_get_info();

// Lấy thông tin cấu hình chân hiện tại của ESP32 thường
BtSlavePins vbot_bt_slave_get_pins();

// Gửi yêu cầu ESP32 thường gửi cấu hình chân sang
void vbot_bt_slave_request_pins();

// Gửi cấu hình chân mới sang ESP32 thường để lưu vào NVS và reboot
void vbot_bt_slave_set_slave_pins(const BtSlavePins &pins);

// Khôi phục chân mặc định trên ESP32 thường
void vbot_bt_slave_reset_slave_pins();
