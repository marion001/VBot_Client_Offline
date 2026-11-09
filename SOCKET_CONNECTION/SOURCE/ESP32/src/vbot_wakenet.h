//Code By: Vũ Tuyển
//Designed by: BootstrapMade
//GitHub VBot: https://github.com/marion001/VBot_Offline.git
//Facebook Group: https://www.facebook.com/groups/1148385343358824
//Facebook: https://www.facebook.com/TWFyaW9uMDAx
//Email: VBot.Assistant@gmail.com


#pragma once

#include <Arduino.h>

#if defined(VBOT_ENABLE_WAKENET) && VBOT_ENABLE_WAKENET
#define VBOT_WAKENET_SUPPORTED 1
#else
#define VBOT_WAKENET_SUPPORTED 0
#endif

enum WakeNetModelId : uint8_t {
  WAKENET_MODEL_HILILI_TTS = 0, // wn9_hilili_tts ("Hi Lily") - Mặc định
  WAKENET_MODEL_ALEXA      = 1, // wn9_alexa ("Alexa")
  WAKENET_MODEL_SOPHIA_TTS = 2, // wn9_sophia_tts ("Sophia")
  WAKENET_MODEL_JARVIS_TTS = 3, // wn9_jarvis_tts ("Jarvis")
  WAKENET_MODEL_CUSTOM_0   = 4, // Tùy chỉnh slot 0
  WAKENET_MODEL_CUSTOM_1   = 5, // Tùy chỉnh slot 1
  WAKENET_MODEL_CUSTOM_2   = 6, // Tùy chỉnh slot 2
  WAKENET_MODEL_CUSTOM_3   = 7, // Tùy chỉnh slot 3
  WAKENET_MODEL_CUSTOM_4   = 8  // Tùy chỉnh slot 4
};

#define WAKENET_DEFAULT_MODEL_COUNT 4
#define WAKENET_MAX_CUSTOM_MODELS 5
#define WAKENET_MAX_TOTAL_MODELS (WAKENET_DEFAULT_MODEL_COUNT + WAKENET_MAX_CUSTOM_MODELS)
#define WAKENET_MODEL_COUNT WAKENET_MAX_TOTAL_MODELS

struct WakeNetCustomSlot {
  bool active = false;
  uint8_t slot = 0;       // 0 đến 4
  char key[32] = "";      // Mã nhận diện model bên trong file .bin
  char name[64] = "";     // Tên hiển thị người dùng đặt trên WebUI
  char filename[32] = ""; // Đường dẫn file lưu trữ trên LittleFS
  uint32_t size = 0;      // Dung lượng file (bytes)
};

// Kiểm tra xem phần cứng hiện tại có hỗ trợ WakeNet không (ESP32-S3 + PSRAM)
bool vbot_wakenet_is_supported();

// Khởi tạo WakeNet engine
bool vbot_wakenet_init(WakeNetModelId initialModel = WAKENET_MODEL_HILILI_TTS);

// Dừng và giải phóng tài nguyên WakeNet
void vbot_wakenet_deinit();

// Chuyển đổi mô hình / từ khóa WakeNet
bool vbot_wakenet_set_model(WakeNetModelId modelId);

// Lấy ID mô hình hiện tại
WakeNetModelId vbot_wakenet_get_model();

// Kiểm tra xem ID có phải là mô hình tùy chỉnh không
bool vbot_wakenet_is_custom(uint8_t modelIdx);

// Lấy tên định danh mô hình (vd: wn9_hilili_tts hoặc custom key)
const char* vbot_wakenet_get_model_key(WakeNetModelId modelId);

// Lấy tên hiển thị thân thiện trên WebUI (vd: "Hi Lily (wn9_hilili_tts)" hoặc "[Tùy chỉnh] ...")
const char* vbot_wakenet_get_model_name(WakeNetModelId modelId);

// Quản lý các mô hình tùy chỉnh người dùng tải lên (tối đa 5 slot)
void vbot_wakenet_init_custom_storage();
int vbot_wakenet_get_custom_count();
bool vbot_wakenet_get_custom_slot(uint8_t slot, WakeNetCustomSlot &out);
bool vbot_wakenet_add_custom_model(const char *tempFilePath, const char *displayName, String &errOut);
bool vbot_wakenet_delete_custom_model(uint8_t slot, String &errOut);

// Nạp các mẫu PCM 16kHz 16-bit Mono vào WakeNet để phân tích
// Trả về true nếu phát hiện từ khóa đánh thức!
bool vbot_wakenet_feed(const int16_t *samples, size_t sample_count);

// Kiểm tra trạng thái đang chạy
bool vbot_wakenet_is_running();
