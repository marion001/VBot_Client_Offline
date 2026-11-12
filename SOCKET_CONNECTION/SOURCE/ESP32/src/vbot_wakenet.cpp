//Code By: Vũ Tuyển
//Designed by: BootstrapMade
//GitHub VBot: https://github.com/marion001/VBot_Offline.git
//Facebook Group: https://www.facebook.com/groups/1148385343358824
//Facebook: https://www.facebook.com/TWFyaW9uMDAx
//Email: VBot.Assistant@gmail.com

#include "vbot_wakenet.h"



static struct {
  const char *key;
  const char *displayName;
} DEFAULT_MODEL_METADATA[WAKENET_DEFAULT_MODEL_COUNT] = {
  {"wn9_hilili_tts", "Hi Lily (wn9_hilili_tts) [Mặc định]"},
  {"wn9_alexa",      "Alexa (wn9_alexa)"},
  {"wn9_sophia_tts", "Sophia (wn9_sophia_tts)"},
  {"wn9_jarvis_tts", "Jarvis (wn9_jarvis_tts)"}
};

#if VBOT_WAKENET_SUPPORTED

#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static SemaphoreHandle_t wakenetMutex = nullptr;

static void ensure_wakenet_mutex() {
  if (!wakenetMutex) {
    wakenetMutex = xSemaphoreCreateRecursiveMutex();
  }
}

struct WakeNetRecursiveLock {
  WakeNetRecursiveLock() {
    ensure_wakenet_mutex();
    if (wakenetMutex) xSemaphoreTakeRecursive(wakenetMutex, portMAX_DELAY);
  }
  ~WakeNetRecursiveLock() {
    if (wakenetMutex) xSemaphoreGiveRecursive(wakenetMutex);
  }
};

#if __has_include("esp_wn_iface.h") && __has_include("esp_wn_models.h") && __has_include("model_path.h")
extern "C" {
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"
#include "esp_aec.h"
#include "esp_ns.h"
#include "esp_agc.h"
}
#define HAVE_NATIVE_ESP_SR 1
#else
#define HAVE_NATIVE_ESP_SR 0
#endif

static const char *TAG = "VBOT_WAKENET";



static WakeNetCustomSlot customSlots[WAKENET_MAX_CUSTOM_MODELS];
static uint8_t *customBuffers[WAKENET_MAX_CUSTOM_MODELS] = {nullptr};

static WakeNetModelId currentModelId = WAKENET_MODEL_HILILI_TTS;
static bool isRunning = false;

#if HAVE_NATIVE_ESP_SR
static srmodel_list_t *srmodels = nullptr;
static const esp_wn_iface_t *wakenetHandle = nullptr;
static model_iface_data_t *modelData = nullptr;

static aec_handle_t aecHandle = nullptr;
static SemaphoreHandle_t aecMutex = nullptr;
static constexpr size_t AEC_FRAME_SAMPLES = 512;
static constexpr size_t AEC_RING_BUFFER_SIZE = 4096; // 8 frames x 512 samples (~256ms)
static int16_t *aecRefBuffer = nullptr;
static volatile size_t aecRefHead = 0;
static volatile size_t aecRefTail = 0;
static volatile size_t aecRefCount = 0;
static volatile bool aecPlaybackActive = false;
static bool aecInitialized = false;

static void ensure_aec_mutex() {
  if (!aecMutex) {
    aecMutex = xSemaphoreCreateMutex();
  }
}
#endif

static const char *CUSTOM_JSON_PATH = "/models/custom_models.json";

#include <sys/stat.h>

static bool vbot_stat_exists(const char *path) {
  char full[64];
  snprintf(full, sizeof(full), "/littlefs%s", path);
  struct stat st;
  return (stat(full, &st) == 0);
}

static void save_custom_models_json() {
  if (!vbot_stat_exists("/models")) {
    LittleFS.mkdir("/models");
  }
  File f = LittleFS.open(CUSTOM_JSON_PATH, "w");
  if (!f) {
    Serial.println("[WakeNet] Lỗi: Không thể mở file custom_models.json để ghi.");
    return;
  }
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < WAKENET_MAX_CUSTOM_MODELS; i++) {
    if (customSlots[i].active) {
      JsonObject item = arr.add<JsonObject>();
      item["slot"] = customSlots[i].slot;
      item["active"] = true;
      item["key"] = customSlots[i].key;
      item["name"] = customSlots[i].name;
      item["filename"] = customSlots[i].filename;
      item["size"] = customSlots[i].size;
    }
  }
  serializeJson(doc, f);
  f.close();
}

void vbot_wakenet_init_custom_storage() {
  for (int i = 0; i < WAKENET_MAX_CUSTOM_MODELS; i++) {
    customSlots[i].slot = i;
    customSlots[i].active = false;
    customSlots[i].key[0] = '\0';
    customSlots[i].name[0] = '\0';
    customSlots[i].filename[0] = '\0';
    customSlots[i].size = 0;
  }

  if (!vbot_stat_exists("/models")) {
    LittleFS.mkdir("/models");
  }

  // Neu file chua ton tai, tao file json rong de tranh log_e cua vfs_api
  if (!vbot_stat_exists(CUSTOM_JSON_PATH)) {
    File f = LittleFS.open(CUSTOM_JSON_PATH, "w");
    if (f) {
      f.print("[]");
      f.close();
    }
    return;
  }

  File f = LittleFS.open(CUSTOM_JSON_PATH, "r");
  if (!f) return;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err || !doc.is<JsonArray>()) {
    Serial.println("[WakeNet] File custom_models.json không đúng định dạng JSON.");
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject item : arr) {
    int slot = item["slot"] | -1;
    if (slot >= 0 && slot < WAKENET_MAX_CUSTOM_MODELS) {
      const char *fname = item["filename"] | "";
      if (LittleFS.exists(fname)) {
        customSlots[slot].active = true;
        customSlots[slot].slot = (uint8_t)slot;
        strncpy(customSlots[slot].key, item["key"] | "", sizeof(customSlots[slot].key) - 1);
        strncpy(customSlots[slot].name, item["name"] | "", sizeof(customSlots[slot].name) - 1);
        strncpy(customSlots[slot].filename, fname, sizeof(customSlots[slot].filename) - 1);
        customSlots[slot].size = item["size"] | 0;
        Serial.printf("[WakeNet] Đã phát hiện Custom Hotword Slot %d: %s (%s)\n",
                      slot, customSlots[slot].name, customSlots[slot].key);
      } else {
        Serial.printf("[WakeNet] Tệp mô hình %s không tồn tại, bỏ qua slot %d\n", fname, slot);
      }
    }
  }
}

int vbot_wakenet_get_custom_count() {
  int count = 0;
  for (int i = 0; i < WAKENET_MAX_CUSTOM_MODELS; i++) {
    if (customSlots[i].active) count++;
  }
  return count;
}

bool vbot_wakenet_get_custom_slot(uint8_t slot, WakeNetCustomSlot &out) {
  if (slot >= WAKENET_MAX_CUSTOM_MODELS) return false;
  out = customSlots[slot];
  return true;
}

bool vbot_wakenet_is_custom(uint8_t modelIdx) {
  return modelIdx >= WAKENET_DEFAULT_MODEL_COUNT && modelIdx < WAKENET_MAX_TOTAL_MODELS;
}

bool vbot_wakenet_is_supported() {
  return psramFound();
}

const char* vbot_wakenet_get_model_key(WakeNetModelId modelId) {
  if (modelId < WAKENET_DEFAULT_MODEL_COUNT) {
    return DEFAULT_MODEL_METADATA[modelId].key;
  }
  uint8_t slot = modelId - WAKENET_DEFAULT_MODEL_COUNT;
  if (slot < WAKENET_MAX_CUSTOM_MODELS && customSlots[slot].active) {
    return customSlots[slot].key;
  }
  return DEFAULT_MODEL_METADATA[WAKENET_MODEL_HILILI_TTS].key;
}

const char* vbot_wakenet_get_model_name(WakeNetModelId modelId) {
  if (modelId < WAKENET_DEFAULT_MODEL_COUNT) {
    return DEFAULT_MODEL_METADATA[modelId].displayName;
  }
  uint8_t slot = modelId - WAKENET_DEFAULT_MODEL_COUNT;
  if (slot < WAKENET_MAX_CUSTOM_MODELS && customSlots[slot].active) {
    return customSlots[slot].name;
  }
  return DEFAULT_MODEL_METADATA[WAKENET_MODEL_HILILI_TTS].displayName;
}

WakeNetModelId vbot_wakenet_get_model() {
  return currentModelId;
}

bool vbot_wakenet_init(WakeNetModelId initialModel) {
  WakeNetRecursiveLock lock;
  if (!vbot_wakenet_is_supported()) {
    Serial.println("[WakeNet] Cảnh báo: Không phát hiện PSRAM, không thể khởi chạy WakeNet.");
    isRunning = false;
    return false;
  }

  if (initialModel >= WAKENET_MAX_TOTAL_MODELS) {
    initialModel = WAKENET_MODEL_HILILI_TTS;
  } else if (vbot_wakenet_is_custom(initialModel)) {
    uint8_t s = initialModel - WAKENET_DEFAULT_MODEL_COUNT;
    if (!customSlots[s].active) {
      Serial.printf("[WakeNet] Custom slot %d chưa được nạp, đưa về mặc định Hi Lily.\n", s);
      initialModel = WAKENET_MODEL_HILILI_TTS;
    }
  }
  currentModelId = initialModel;

#if HAVE_NATIVE_ESP_SR
  if (!srmodels) {
    srmodels = esp_srmodel_init("model");
    if (!srmodels) {
      Serial.println("[WakeNet] Không tìm thấy partition 'model' hoặc file model trên Flash.");
      isRunning = false;
      return false;
    }

    // Nạp các mô hình tùy chỉnh từ LittleFS vào PSRAM và gắn vào danh sách srmodels
    for (int s = 0; s < WAKENET_MAX_CUSTOM_MODELS; s++) {
      if (!customSlots[s].active) continue;
      if (!LittleFS.exists(customSlots[s].filename)) {
        customSlots[s].active = false;
        continue;
      }
      File f = LittleFS.open(customSlots[s].filename, "r");
      if (!f) continue;
      size_t fsize = f.size();
      if (fsize < 1000 || fsize > 2000000) {
        f.close();
        continue;
      }

      if (customBuffers[s]) {
        free(customBuffers[s]);
        customBuffers[s] = nullptr;
      }
      customBuffers[s] = (uint8_t*)heap_caps_malloc(fsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!customBuffers[s]) {
        Serial.printf("[WakeNet] Không đủ bộ nhớ PSRAM để load custom model slot %d (%u bytes)\n", s, (unsigned)fsize);
        f.close();
        continue;
      }
      size_t readBytes = f.read(customBuffers[s], fsize);
      f.close();
      if (readBytes != fsize) {
        free(customBuffers[s]);
        customBuffers[s] = nullptr;
        continue;
      }

      uint8_t *raw = customBuffers[s];
      int32_t model_num = *(int32_t*)(raw);
      if (model_num < 1) {
        free(customBuffers[s]);
        customBuffers[s] = nullptr;
        continue;
      }
      int32_t file_num = *(int32_t*)(raw + 36);
      if (file_num < 1 || file_num > 10) {
        free(customBuffers[s]);
        customBuffers[s] = nullptr;
        continue;
      }

      srmodel_data_t *cdata = (srmodel_data_t*)malloc(sizeof(srmodel_data_t));
      cdata->num = file_num;
      cdata->files = (char**)malloc(sizeof(char*) * file_num);
      cdata->data = (char**)malloc(sizeof(char*) * file_num);
      cdata->sizes = (int*)malloc(sizeof(int) * file_num);

      char *ptr = (char*)(raw + 40);
      char *model_info_str = nullptr;
      for (int j = 0; j < file_num; j++) {
        cdata->files[j] = ptr;
        ptr += 32;
        int32_t offset = *(int32_t*)ptr;
        ptr += 4;
        int32_t sz = *(int32_t*)ptr;
        ptr += 4;
        cdata->data[j] = (char*)(raw + offset);
        cdata->sizes[j] = sz;
        if (strcmp(cdata->files[j], "_MODEL_INFO_") == 0) {
          char *raw_info = cdata->data[j];
          int info_sz = cdata->sizes[j];
          while (info_sz > 0) {
            if (*raw_info == '#') {
              while (*raw_info != '\n' && info_sz > 1) { raw_info++; info_sz--; }
              raw_info++; info_sz--;
              continue;
            } else if (info_sz > 0) {
              model_info_str = (char*)malloc(info_sz + 1);
              memcpy(model_info_str, raw_info, info_sz);
              if (model_info_str[info_sz - 1] == '\n') model_info_str[info_sz - 1] = '\0';
              model_info_str[info_sz] = '\0';
              break;
            }
          }
        }
      }

      int oldNum = srmodels->num;
      srmodels->num = oldNum + 1;
      srmodels->model_name = (char**)realloc(srmodels->model_name, sizeof(char*) * srmodels->num);
      srmodels->model_info = (char**)realloc(srmodels->model_info, sizeof(char*) * srmodels->num);
      srmodels->model_data = (srmodel_data_t**)realloc(srmodels->model_data, sizeof(srmodel_data_t*) * srmodels->num);

      srmodels->model_name[oldNum] = strdup(customSlots[s].key);
      srmodels->model_info[oldNum] = model_info_str ? model_info_str : strdup(customSlots[s].key);
      srmodels->model_data[oldNum] = cdata;

      Serial.printf("[WakeNet] Đã nạp Custom Hotword Slot %d: key='%s', name='%s', size=%u\n",
                    s, customSlots[s].key, customSlots[s].name, (unsigned)fsize);
    }
  }

  if (srmodels) {
    const char *modelKey = vbot_wakenet_get_model_key(currentModelId);
    char *wn_name = esp_srmodel_filter(srmodels, ESP_WN_PREFIX, modelKey);
    if (!wn_name) {
      Serial.printf("[WakeNet] Không tìm thấy model '%s' trong partition model.\n", modelKey);
      isRunning = false;
      return false;
    }
    wakenetHandle = esp_wn_handle_from_name(wn_name);
    if (!wakenetHandle) {
      Serial.printf("[WakeNet] Không lấy được handle cho '%s'.\n", wn_name);
      isRunning = false;
      return false;
    }
    modelData = wakenetHandle->create(wn_name, DET_MODE_95);
    if (!modelData) {
      Serial.printf("[WakeNet] Khởi tạo model '%s' thất bại.\n", wn_name);
      isRunning = false;
      return false;
    }
    isRunning = true;
    vbot_aec_init();
    vbot_ns_init(1);
    vbot_agc_init(12);
    int chunk = wakenetHandle->get_samp_chunksize(modelData);
    Serial.printf("[WakeNet] Khởi tạo thành công model: %s (%s), chunk=%d samples\n",
                  modelKey, vbot_wakenet_get_model_name(currentModelId), chunk);
    return true;
  }
#endif

  isRunning = false;
  return false;
}

void vbot_wakenet_deinit() {
  WakeNetRecursiveLock lock;
  isRunning = false;
  vbot_aec_deinit();
  vbot_ns_deinit();
  vbot_agc_deinit();

#if HAVE_NATIVE_ESP_SR
  if (wakenetHandle && modelData) {
    wakenetHandle->destroy(modelData);
    modelData = nullptr;
    wakenetHandle = nullptr;
  }
  if (srmodels) {
    esp_srmodel_deinit(srmodels);
    srmodels = nullptr;
  }
  for (int s = 0; s < WAKENET_MAX_CUSTOM_MODELS; s++) {
    if (customBuffers[s]) {
      free(customBuffers[s]);
      customBuffers[s] = nullptr;
    }
  }
#endif
  Serial.println("[WakeNet] Đã dừng và giải phóng tài nguyên WakeNet.");
}

bool vbot_wakenet_set_model(WakeNetModelId modelId) {
  WakeNetRecursiveLock lock;
  if (modelId >= WAKENET_MAX_TOTAL_MODELS) return false;
  if (currentModelId == modelId && isRunning) return true;

  bool wasRunning = isRunning;
  if (wasRunning) vbot_wakenet_deinit();

  currentModelId = modelId;
  Serial.printf("[WakeNet] Chuyển đổi sang model: %s\n", vbot_wakenet_get_model_name(currentModelId));

  if (wasRunning) {
    return vbot_wakenet_init(currentModelId);
  }
  return true;
}

bool vbot_wakenet_is_running() {
  return isRunning;
}

bool vbot_wakenet_feed(const int16_t *samples, size_t sample_count) {
  if (!isRunning || !samples || sample_count == 0) return false;
  if (!wakenetMutex) return false;
  if (xSemaphoreTakeRecursive(wakenetMutex, 0) != pdTRUE) {
    return false;
  }

  bool detected = false;
#if HAVE_NATIVE_ESP_SR
  if (isRunning && wakenetHandle && modelData) {
    int res = wakenetHandle->detect(modelData, (int16_t*)samples);
    if (res > 0) {
      Serial.printf("[WakeNet] Phát hiện từ khóa: %s (id=%d)!\n", vbot_wakenet_get_model_key(currentModelId), res);
      detected = true;
    }
  }
#endif

  xSemaphoreGiveRecursive(wakenetMutex);
  return detected;
}

// ============================================================================
// Triển khai AEC (Acoustic Echo Cancellation) & Barge-in
// ============================================================================

bool vbot_aec_init() {
#if HAVE_NATIVE_ESP_SR
  if (!psramFound()) {
    Serial.println("[AEC] Cảnh báo: Cần PSRAM để chạy AEC.");
    return false;
  }
  ensure_aec_mutex();
  if (aecHandle) return true;

  if (!aecRefBuffer) {
    aecRefBuffer = (int16_t*)heap_caps_malloc(AEC_RING_BUFFER_SIZE * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!aecRefBuffer) {
      Serial.println("[AEC] Không đủ bộ nhớ PSRAM cho bộ đệm tham chiếu AEC.");
      return false;
    }
    memset(aecRefBuffer, 0, AEC_RING_BUFFER_SIZE * sizeof(int16_t));
  }
  aecRefHead = 0;
  aecRefTail = 0;
  aecRefCount = 0;
  aecPlaybackActive = false;

  // Khởi tạo AEC pro: frame_length = 32ms (512 samples ở 16kHz), nch = 1 mic, mode = 5 (ESP32-S3 accelerated)
  aecHandle = aec_pro_create(32, 1, 5);
  if (!aecHandle) {
    Serial.println("[AEC] Khởi tạo aec_pro_create thất bại.");
    return false;
  }
  aecInitialized = true;
  Serial.println("[AEC] Khởi tạo Acoustic Echo Cancellation (AEC Mode 5 - ESP32S3) thành công!");
  return true;
#else
  return false;
#endif
}

void vbot_aec_deinit() {
#if HAVE_NATIVE_ESP_SR
  ensure_aec_mutex();
  if (aecMutex) xSemaphoreTake(aecMutex, portMAX_DELAY);
  aecInitialized = false;
  aecPlaybackActive = false;
  if (aecHandle) {
    aec_destroy(aecHandle);
    aecHandle = nullptr;
  }
  if (aecRefBuffer) {
    free(aecRefBuffer);
    aecRefBuffer = nullptr;
  }
  aecRefHead = 0;
  aecRefTail = 0;
  aecRefCount = 0;
  if (aecMutex) xSemaphoreGive(aecMutex);
  Serial.println("[AEC] Đã giải phóng tài nguyên AEC.");
#endif
}

bool vbot_aec_is_active() {
#if HAVE_NATIVE_ESP_SR
  return aecInitialized && (aecHandle != nullptr);
#else
  return false;
#endif
}

void vbot_aec_set_playback_active(bool active) {
#if HAVE_NATIVE_ESP_SR
  aecPlaybackActive = active;
  if (!active && aecMutex && xSemaphoreTake(aecMutex, 0) == pdTRUE) {
    aecRefHead = 0;
    aecRefTail = 0;
    aecRefCount = 0;
    xSemaphoreGive(aecMutex);
  }
#endif
}

bool vbot_aec_get_playback_active() {
#if HAVE_NATIVE_ESP_SR
  return aecPlaybackActive;
#else
  return false;
#endif
}

void vbot_aec_feed_ref(const int16_t *samples, size_t sample_count) {
#if HAVE_NATIVE_ESP_SR
  if (!aecInitialized || !aecRefBuffer || !samples || sample_count == 0) return;
  if (!aecMutex) return;
  if (xSemaphoreTake(aecMutex, 0) != pdTRUE) return;

  for (size_t i = 0; i < sample_count; i++) {
    aecRefBuffer[aecRefHead] = samples[i];
    aecRefHead = (aecRefHead + 1) % AEC_RING_BUFFER_SIZE;
    if (aecRefCount < AEC_RING_BUFFER_SIZE) {
      aecRefCount++;
    } else {
      aecRefTail = (aecRefTail + 1) % AEC_RING_BUFFER_SIZE;
    }
  }
  aecPlaybackActive = true;
  xSemaphoreGive(aecMutex);
#endif
}

void vbot_aec_process(const int16_t *mic_in, int16_t *clean_out) {
  if (!mic_in || !clean_out) return;
#if HAVE_NATIVE_ESP_SR
  if (!aecInitialized || !aecHandle || !aecPlaybackActive) {
    memcpy(clean_out, mic_in, AEC_FRAME_SAMPLES * sizeof(int16_t));
    return;
  }

  int16_t refFrame[AEC_FRAME_SAMPLES];
  bool haveRef = false;
  if (aecMutex && xSemaphoreTake(aecMutex, 0) == pdTRUE) {
    if (aecRefCount >= AEC_FRAME_SAMPLES) {
      for (size_t i = 0; i < AEC_FRAME_SAMPLES; i++) {
        refFrame[i] = aecRefBuffer[aecRefTail];
        aecRefTail = (aecRefTail + 1) % AEC_RING_BUFFER_SIZE;
      }
      aecRefCount -= AEC_FRAME_SAMPLES;
      haveRef = true;
    } else {
      // Hết mẫu tham chiếu trong đệm
      aecPlaybackActive = false;
    }
    xSemaphoreGive(aecMutex);
  }

  if (haveRef) {
    aec_process(aecHandle, (int16_t*)mic_in, refFrame, clean_out);
  } else {
    memcpy(clean_out, mic_in, AEC_FRAME_SAMPLES * sizeof(int16_t));
  }
#else
  memcpy(clean_out, mic_in, 512 * sizeof(int16_t));
#endif
}

// ============================================================================
// Triển khai NS (Noise Suppression) & AGC (Auto Gain Control)
// ============================================================================

static ns_handle_t nsHandle = nullptr;
static bool nsInitialized = false;

static void *agcHandle = nullptr;
static bool agcInitialized = false;

// Buffer FIFO để đồng bộ khung 512 mẫu (AEC) và 160 mẫu (NS & AGC)
static constexpr size_t DSP_CHUNK_SIZE = 160; // 10ms ở 16kHz
static constexpr size_t DSP_PREFILL_SIZE = 160;
static constexpr size_t DSP_TOTAL_FIFO = DSP_PREFILL_SIZE + 512; // 672 mẫu
static int16_t dspBuffer[DSP_TOTAL_FIFO] = {0};
static bool dspBufferPrefilled = false;

bool vbot_ns_init(int mode) {
#if HAVE_NATIVE_ESP_SR
  if (nsHandle) return true;
  mode = constrain(mode, 0, 2);
  nsHandle = ns_pro_create(10, mode, 16000);
  if (!nsHandle) {
    Serial.println("[NS] Khởi tạo ns_pro_create thất bại.");
    return false;
  }
  nsInitialized = true;
  Serial.printf("[NS] Khởi tạo Noise Suppression (Chống ồn Mode %d) thành công!\n", mode);
  return true;
#else
  return false;
#endif
}

void vbot_ns_deinit() {
#if HAVE_NATIVE_ESP_SR
  if (nsHandle) {
    ns_destroy(nsHandle);
    nsHandle = nullptr;
  }
  nsInitialized = false;
  Serial.println("[NS] Đã giải phóng tài nguyên NS.");
#endif
}

bool vbot_ns_is_active() {
  return nsInitialized && (nsHandle != nullptr);
}

bool vbot_agc_init(int target_gain_db) {
#if HAVE_NATIVE_ESP_SR
  if (agcHandle) return true;
  target_gain_db = constrain(target_gain_db, 0, 30);
  agcHandle = esp_agc_open(3, 16000);
  if (!agcHandle) {
    Serial.println("[AGC] Khởi tạo esp_agc_open thất bại.");
    return false;
  }
  set_agc_config(agcHandle, target_gain_db, 1, 3);
  agcInitialized = true;
  Serial.printf("[AGC] Khởi tạo Auto Gain Control (Khuếch đại tự động %d dB) thành công!\n", target_gain_db);
  return true;
#else
  return false;
#endif
}

void vbot_agc_deinit() {
#if HAVE_NATIVE_ESP_SR
  if (agcHandle) {
    esp_agc_close(agcHandle);
    agcHandle = nullptr;
  }
  agcInitialized = false;
  Serial.println("[AGC] Đã giải phóng tài nguyên AGC.");
#endif
}

bool vbot_agc_is_active() {
  return agcInitialized && (agcHandle != nullptr);
}

void vbot_audio_dsp_process(const int16_t *mic_in, int16_t *clean_out, size_t sample_count, bool enable_ns, bool enable_agc) {
  if (!mic_in || !clean_out || sample_count == 0) return;

  // Bước 1: Chạy lọc hồi tiếp âm thanh loa (AEC)
  vbot_aec_process(mic_in, clean_out);

  // Nếu không cần chạy NS lẫn AGC, trả về ngay sau AEC
  if ((!enable_ns || !nsHandle) && (!enable_agc || !agcHandle)) {
    return;
  }

  // Khởi tạo đệm prefill 10ms nếu chưa có
  if (!dspBufferPrefilled) {
    memset(dspBuffer, 0, sizeof(dspBuffer));
    dspBufferPrefilled = true;
  }

  // Nạp 512 mẫu sau AEC vào đuôi đệm (vị trí từ 160 đến 671)
  memcpy(&dspBuffer[DSP_PREFILL_SIZE], clean_out, sample_count * sizeof(int16_t));

  // Bước 2 & 3: Chạy 4 khối 160 mẫu (640 mẫu) qua NS và AGC
  int16_t chunk[DSP_CHUNK_SIZE];
  for (size_t b = 0; b < 4; b++) {
    int16_t *blockPtr = &dspBuffer[b * DSP_CHUNK_SIZE];

    // Chống ồn NS
    if (enable_ns && nsHandle) {
      ns_process(nsHandle, blockPtr, chunk);
      memcpy(blockPtr, chunk, DSP_CHUNK_SIZE * sizeof(int16_t));
    }

    // Tự động bù âm lượng AGC
    if (enable_agc && agcHandle) {
      esp_agc_process(agcHandle, blockPtr, chunk, DSP_CHUNK_SIZE, 16000);
      memcpy(blockPtr, chunk, DSP_CHUNK_SIZE * sizeof(int16_t));
    }
  }

  // Xuất đúng 512 mẫu sạch ra clean_out
  memcpy(clean_out, dspBuffer, sample_count * sizeof(int16_t));

  // Dịch 160 mẫu còn lại (672 - 512 = 160) về đầu đệm cho frame kế tiếp
  memmove(dspBuffer, &dspBuffer[sample_count], DSP_PREFILL_SIZE * sizeof(int16_t));
}

bool vbot_wakenet_add_custom_model(const char *tempFilePath, const char *displayName, String &errOut) {
  WakeNetRecursiveLock lock;
  if (!LittleFS.exists(tempFilePath)) {
    errOut = "Không tìm thấy tệp tạm tải lên.";
    return false;
  }
  File f = LittleFS.open(tempFilePath, "r");
  if (!f) {
    errOut = "Không thể đọc tệp tạm.";
    return false;
  }
  size_t fsize = f.size();
  if (fsize < 1000 || fsize > 2000000) {
    f.close();
    errOut = "Kích thước tệp không hợp lệ (phải từ 50KB đến 2MB).";
    return false;
  }

  uint8_t hdr[160];
  if (f.read(hdr, sizeof(hdr)) < sizeof(hdr)) {
    f.close();
    errOut = "Tệp quá ngắn, không đúng định dạng WakeNet binary.";
    return false;
  }
  f.close();

  int32_t model_num = *(int32_t*)(hdr);
  if (model_num < 1 || model_num > 10) {
    errOut = "Header không hợp lệ: model_num không đúng.";
    return false;
  }

  char mname[33] = {0};
  memcpy(mname, hdr + 4, 32);
  mname[32] = '\0';
  if (strlen(mname) < 2 || !strstr(mname, "wn")) {
    errOut = "Tên model trong file không đúng quy chuẩn WakeNet (phải bắt đầu bằng 'wn').";
    return false;
  }

  int32_t file_num = *(int32_t*)(hdr + 36);
  if (file_num < 2 || file_num > 10) {
    errOut = "Số lượng file con trong model không hợp lệ.";
    return false;
  }

  int freeSlot = -1;
  for (int s = 0; s < WAKENET_MAX_CUSTOM_MODELS; s++) {
    if (!customSlots[s].active) {
      freeSlot = s;
      break;
    }
  }

  if (freeSlot == -1) {
    errOut = "Đã đạt giới hạn tối đa 5 hotword tùy chỉnh. Vui lòng xóa bớt trước khi thêm mới.";
    return false;
  }

  char targetPath[32];
  snprintf(targetPath, sizeof(targetPath), "/models/custom_%d.bin", freeSlot);

  if (LittleFS.exists(targetPath)) {
    LittleFS.remove(targetPath);
  }

  if (!LittleFS.rename(tempFilePath, targetPath)) {
    File src = LittleFS.open(tempFilePath, "r");
    File dst = LittleFS.open(targetPath, "w");
    if (!src || !dst) {
      if (src) src.close();
      if (dst) dst.close();
      errOut = "Không thể ghi tệp vào LittleFS.";
      return false;
    }
    uint8_t buf[512];
    while (src.available()) {
      size_t n = src.read(buf, sizeof(buf));
      dst.write(buf, n);
    }
    src.close();
    dst.close();
    LittleFS.remove(tempFilePath);
  }

  customSlots[freeSlot].active = true;
  customSlots[freeSlot].slot = (uint8_t)freeSlot;
  strncpy(customSlots[freeSlot].key, mname, sizeof(customSlots[freeSlot].key) - 1);
  customSlots[freeSlot].size = fsize;
  strncpy(customSlots[freeSlot].filename, targetPath, sizeof(customSlots[freeSlot].filename) - 1);

  if (displayName && strlen(displayName) > 0) {
    snprintf(customSlots[freeSlot].name, sizeof(customSlots[freeSlot].name), "%s (%s)", displayName, mname);
  } else {
    snprintf(customSlots[freeSlot].name, sizeof(customSlots[freeSlot].name), "Hotword [%s]", mname);
  }

  save_custom_models_json();

  if (isRunning) {
    vbot_wakenet_deinit();
    vbot_wakenet_init(currentModelId);
  }

  return true;
}

bool vbot_wakenet_delete_custom_model(uint8_t slot, String &errOut) {
  WakeNetRecursiveLock lock;
  if (slot >= WAKENET_MAX_CUSTOM_MODELS) {
    errOut = "Slot hotword tùy chỉnh không hợp lệ (chỉ được phép xóa slot 0-4).";
    return false;
  }
  if (!customSlots[slot].active) {
    errOut = "Slot này không tồn tại hoặc đã được xóa trước đó.";
    return false;
  }

  if (LittleFS.exists(customSlots[slot].filename)) {
    LittleFS.remove(customSlots[slot].filename);
  }

  customSlots[slot].active = false;
  customSlots[slot].key[0] = '\0';
  customSlots[slot].name[0] = '\0';
  customSlots[slot].filename[0] = '\0';
  customSlots[slot].size = 0;

  save_custom_models_json();

  WakeNetModelId deletedModelId = (WakeNetModelId)(WAKENET_DEFAULT_MODEL_COUNT + slot);
  if (currentModelId == deletedModelId) {
    currentModelId = WAKENET_MODEL_HILILI_TTS;
  }

  if (isRunning) {
    vbot_wakenet_deinit();
    vbot_wakenet_init(currentModelId);
  }

  return true;
}

#else // !VBOT_WAKENET_SUPPORTED

bool vbot_wakenet_is_supported() { return false; }
bool vbot_wakenet_init(WakeNetModelId) { return false; }
void vbot_wakenet_deinit() {}
bool vbot_wakenet_set_model(WakeNetModelId) { return false; }
WakeNetModelId vbot_wakenet_get_model() { return WAKENET_MODEL_HILILI_TTS; }
bool vbot_wakenet_is_custom(uint8_t) { return false; }
const char* vbot_wakenet_get_model_key(WakeNetModelId id) {
  if (id < WAKENET_DEFAULT_MODEL_COUNT) {
    return DEFAULT_MODEL_METADATA[id].key;
  }
  return DEFAULT_MODEL_METADATA[WAKENET_MODEL_HILILI_TTS].key;
}
const char* vbot_wakenet_get_model_name(WakeNetModelId id) {
  if (id < WAKENET_DEFAULT_MODEL_COUNT) {
    return DEFAULT_MODEL_METADATA[id].displayName;
  }
  return DEFAULT_MODEL_METADATA[WAKENET_MODEL_HILILI_TTS].displayName;
}
void vbot_wakenet_init_custom_storage() {}
int vbot_wakenet_get_custom_count() { return 0; }
bool vbot_wakenet_get_custom_slot(uint8_t, WakeNetCustomSlot&) { return false; }
bool vbot_wakenet_add_custom_model(const char*, const char*, String &errOut) {
  errOut = "Phần cứng không hỗ trợ WakeNet.";
  return false;
}
bool vbot_wakenet_delete_custom_model(uint8_t, String &errOut) {
  errOut = "Phần cứng không hỗ trợ WakeNet.";
  return false;
}
bool vbot_wakenet_feed(const int16_t*, size_t) { return false; }
bool vbot_wakenet_is_running() { return false; }

// ============================================================================
// AEC / NS / AGC Stubs cho cấu hình không bật WakeNet
// ============================================================================
bool vbot_aec_init() { return false; }
void vbot_aec_deinit() {}
bool vbot_aec_is_active() { return false; }
void vbot_aec_set_playback_active(bool) {}
bool vbot_aec_get_playback_active() { return false; }
void vbot_aec_feed_ref(const int16_t*, size_t) {}
void vbot_aec_process(const int16_t *mic_in, int16_t *clean_out) {
  if (mic_in && clean_out) {
    memcpy(clean_out, mic_in, 512 * sizeof(int16_t));
  }
}
bool vbot_ns_init(int) { return false; }
void vbot_ns_deinit() {}
bool vbot_ns_is_active() { return false; }
bool vbot_agc_init(int) { return false; }
void vbot_agc_deinit() {}
bool vbot_agc_is_active() { return false; }
void vbot_audio_dsp_process(const int16_t *mic_in, int16_t *clean_out, size_t sample_count, bool, bool) {
  if (mic_in && clean_out && sample_count > 0) {
    memcpy(clean_out, mic_in, sample_count * sizeof(int16_t));
  }
}

#endif
