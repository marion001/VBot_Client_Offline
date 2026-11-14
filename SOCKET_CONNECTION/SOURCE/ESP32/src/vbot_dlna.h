//Code By: Vũ Tuyển
//Designed by: BootstrapMade
//GitHub VBot: https://github.com/marion001/VBot_Offline.git
//Facebook Group: https://www.facebook.com/groups/1148385343358824
//Facebook: https://www.facebook.com/TWFyaW9uMDAx
//Email: VBot.Assistant@gmail.com

#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

typedef void (*DlnaPlayCallback)(const char *url, const char *title);
typedef void (*DlnaStopCallback)();
typedef void (*DlnaPauseCallback)();
typedef void (*DlnaVolumeCallback)(int volume);

struct DlnaCallbacks {
  DlnaPlayCallback onPlay = nullptr;
  DlnaStopCallback onStop = nullptr;
  DlnaPauseCallback onPause = nullptr;
  DlnaVolumeCallback onVolume = nullptr;
};

// Khởi tạo DLNA MediaRenderer, đăng ký route vào AsyncWebServer và chạy task SSDP
bool vbot_dlna_init(AsyncWebServer &server, const char *friendlyName, const DlnaCallbacks &callbacks);

// Dừng và giải phóng tài nguyên DLNA
void vbot_dlna_stop();

// Kiểm tra DLNA có đang chạy hay không
bool vbot_dlna_is_running();

// Cập nhật trạng thái phát hiện tại cho DLNA query ("PLAYING", "STOPPED", "PAUSED_PLAYBACK")
void vbot_dlna_set_state(const char *state);

// Cập nhật âm lượng hiện tại (0-100)
void vbot_dlna_set_volume(int vol);

// Lấy âm lượng hiện tại
int vbot_dlna_get_volume();

// Lấy thông tin bài hát hiện tại
const char* vbot_dlna_get_title();
const char* vbot_dlna_get_uri();
