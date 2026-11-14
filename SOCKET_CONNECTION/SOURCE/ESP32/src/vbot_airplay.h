//Code By: Vũ Tuyển
//Designed by: BootstrapMade
//GitHub VBot: https://github.com/marion001/VBot_Offline.git
//Facebook Group: https://www.facebook.com/groups/1148385343358824
//Facebook: https://www.facebook.com/TWFyaW9uMDAx
//Email: VBot.Assistant@gmail.com

#pragma once

#include <Arduino.h>

typedef void (*AirPlayPlayCallback)();
typedef void (*AirPlayStopCallback)();
typedef void (*AirPlayVolumeCallback)(int volume);
typedef void (*AirPlayPeakCallback)(uint16_t peak);

struct AirPlayCallbacks {
  AirPlayPlayCallback onPlay = nullptr;
  AirPlayStopCallback onStop = nullptr;
  AirPlayVolumeCallback onVolume = nullptr;
  AirPlayPeakCallback onPeak = nullptr;
};

// Cấu hình chân phần cứng I2S DAC cho AirPlay
void vbot_airplay_set_dac_pins(int8_t bclk, int8_t lrc, int8_t dout);

// Khởi tạo máy chủ Apple AirPlay (mDNS Bonjour + RTSP Server + Audio UDP)
bool vbot_airplay_init(const char *friendlyName, const AirPlayCallbacks &callbacks);

// Dừng máy chủ Apple AirPlay
void vbot_airplay_stop();

// Đăng ký lại mDNS khi WiFi kết nối lại
void vbot_airplay_register_mdns();

// Kiểm tra xem AirPlay có đang chạy không
bool vbot_airplay_is_running();

// Kiểm tra xem iPhone có đang kết nối/phát nhạc AirPlay không
bool vbot_airplay_is_streaming();

// Lấy âm lượng hiện tại từ AirPlay (0-100)
int vbot_airplay_get_volume();

// Cài đặt/Gỡ bỏ driver I2S cho AirPlay (Stereo 44.1kHz 16-bit)
bool vbot_airplay_install_i2s();
void vbot_airplay_uninstall_i2s();

