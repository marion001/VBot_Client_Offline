# Hướng Dẫn ESP32-S3 N16R8 (16MB Flash, 8MB Octal PSRAM)

Tài liệu chi tiết về cấu hình, phân vùng bộ nhớ, quy trình build, nạp firmware và hệ thống nhận diện từ khóa đánh thức **WakeNet9 On-Device** cho bo mạch **ESP32-S3 DevKit N16R8**.

---

## 1. Thông số kỹ thuật phần cứng

- **Chip**: ESP32-S3 (Xtensa® Dual-Core 32-bit LX7, 240 MHz, tập lệnh tăng tốc AI Vector).
- **Flash**: 16 MB (Chế độ `QIO`, tốc độ 80 MHz).
- **PSRAM**: 8 MB Octal SPI (`OPI`, tốc độ 80 MHz).
- **Environment PlatformIO**: `esp32s3-n16r8`.
- **Bảng phân vùng**: [`partitions_vbot_ota_16mb.csv`](partitions_vbot_ota_16mb.csv).

---

## 2. Bảng phân vùng bộ nhớ Flash (16 MB)

```csv
# Name,   Type, SubType,  Offset,    Size,     Flags
nvs,      data, nvs,      0x9000,    0x5000,
otadata,  data, ota,      0xe000,    0x2000,
app0,     app,  ota_0,    0x10000,   0x400000,
app1,     app,  ota_1,    0x410000,  0x400000,
littlefs, data, spiffs,   0x810000,  0x3F0000,
model,    data, ,         0xC00000,  0x3F0000,
```

### Chi tiết các phân vùng:
- `0x0000`: **Bootloader** (`bootloader.bin`).
- `0x8000`: **Partition Table** (`partitions.bin`).
- `0x9000`: **NVS** (Lưu cấu hình Wi-Fi, pinout, thông số client).
- `0xE000`: **OTA Data** (Quản lý phân vùng khởi động OTA).
- `0x10000`: **App 0 (OTA_0)** (`firmware.bin` - dung lượng 4MB).
- `0x410000`: **App 1 (OTA_1)** (Phân vùng OTA dự phòng - dung lượng 4MB).
- `0x810000`: **LittleFS** (`littlefs.bin` - dung lượng ~4MB): Chứa giao diện WebUI gzip (`index.html.gz`, `busy.html.gz`), file âm thanh thông báo (`/sound/`) và thư mục lưu trữ Hotword tùy chỉnh người dùng tải lên (`/models/`).
- `0xC00000`: **Model WakeNet** (`srmodels.bin` - dung lượng ~4MB): Phân vùng dữ liệu thô chứa 4 mô hình WakeNet9 mặc định (*Hi Lily, Alexa, Sophia, Jarvis*) được mmap trực tiếp vào bộ nhớ.

---

## 3. Offset nạp bằng Espressif Flash Download Tool

Khi sử dụng phần mềm **Espressif Flash Download Tool**:
1. Chọn chip: `ESP32-S3`, WorkMode: `Develop`.
2. Thiết lập: Flash Size: `16MB` (128Mbit), Flash Speed: `80MHz`, Flash Mode: `QIO`.
3. Điền các file và địa chỉ offset chính xác như bảng sau:

| Tệp nạp | Địa chỉ Offset | Mô tả |
|---|:---:|---|
| `bootloader.bin` | `0x0000` | Trình khởi động ESP32-S3 (Tuyệt đối **không** dùng `0x1000`) |
| `partitions.bin` | `0x8000` | Bảng phân vùng 16MB |
| `firmware.bin` | `0x10000` | Chương trình chính VBot Client |
| `littlefs.bin` | `0x810000` | Hệ thống tệp WebUI và âm thanh |
| `srmodels.bin` | `0xC00000` | Gói 4 mô hình WakeNet9 On-Device mặc định (trong thư mục `models/`) |

---

## 4. Build và nạp bằng PlatformIO

Đổi `COM6` thành cổng COM kết nối thực tế trên máy tính của bạn:

### 4.1. Biên dịch và nạp chương trình:
```powershell
# 1. Biên dịch firmware
pio run -e esp32s3-n16r8

# 2. Đóng gói hệ thống tệp LittleFS
pio run -e esp32s3-n16r8 -t buildfs

# 3. Nạp firmware xuống ESP32-S3
pio run -e esp32s3-n16r8 -t upload --upload-port COM6

# 4. Nạp LittleFS xuống ESP32-S3
pio run -e esp32s3-n16r8 -t uploadfs --upload-port COM6
```

### 4.2. Nạp phân vùng mô hình WakeNet9 (`srmodels.bin`):
Phân vùng `model` chỉ cần nạp một lần đầu tiên hoặc khi cập nhật gói model mặc định:
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py" --chip esp32s3 --port COM6 write_flash 0xC00000 models/srmodels.bin
```

---

## 5. Tính năng WakeNet9 On-Device và Quản Lý Hotword

ESP32-S3 N16R8 được tích hợp công nghệ nhận diện giọng nói đánh thức tại biên **WakeNet9**:

### 5.1. Ưu điểm nổi bật:
- **Độ trễ 0ms**: Đánh thức tức thì không phụ thuộc vào đường truyền mạng Wi-Fi.
- **Tiết kiệm mạng**: ESP32 ở trạng thái chờ không cần stream âm thanh lên server, chỉ gửi audio sau khi đã nhận diện đúng từ khóa đánh thức.
- **Hoạt động song song**: Hỗ trợ chuyển đổi linh hoạt giữa 3 chế độ:
  1. *WakeNet On-Device (ESP32-S3)*.
  2. *Sử Dụng Hotword Ở Máy Chủ VBot* (Server Stream "VBot ơi").
  3. *Không Sử Dụng* (Chỉ đánh thức bằng nút nhấn hoặc API).

### 5.2. Tab riêng biệt trên WebUI: `Cấu Hình Hotword WakeUP`
- Truy cập IP của thiết bị trên trình duyệt (ví dụ: `http://192.168.14.147`).
- Chuyển sang tab **`Cấu Hình Hotword WakeUP`**:
  - **Chế độ Hotword / Đánh thức**: Bật/tắt hoặc chọn chế độ.
  - **Từ khóa WakeNet On-Device**: Chọn từ khóa hoạt động mong muốn.
  - **Bảng Quản Lý Hotword On-Device**:
    - **4 Hotword mặc định**: *Hi Lily*, *Alexa*, *Sophia*, *Jarvis* (Cố định, khóa bảo vệ, không thể xóa).
    - **Tối đa 5 Hotword tùy chỉnh**: Hiển thị tên, key nhận diện, dung lượng và nút **Xóa**.
    - **Form nạp Hotword**: Cho phép chọn file `.bin` và nhập tên gợi nhớ để nạp trực tiếp qua trình duyệt.

### 5.3. Thư viện 18 mô hình WakeNet9 đã đóng gói sẵn:
Trong thư mục [`models/wakenet_bin_models/`](models/wakenet_bin_models/) có sẵn 18 mô hình `.bin` để bạn tải lên bất kỳ lúc nào:
*Alexa, Astrolabe, BlueChip, Computer, Hey Ily, Hey Ivy, Hey Kira, Hey Wanda, Hey Willow, Hi Andy, Hi ESP, Hi Jason, Hi Joy, Hi Lily, Hi M Five, Jarvis, Mycroft, Sophia*.

Chi tiết xem tại [`models/README.md`](models/README.md).

### 5.4. Đóng gói mô hình tùy chỉnh mới:
Dùng tool [`scripts/pack_custom_hotword.py`](scripts/pack_custom_hotword.py):
```powershell
python scripts/pack_custom_hotword.py -d "duong_dan_thu_muc_model" -o "models/wakenet_bin_models/ten_model.bin" -n "wn9_ten_model"
```

---

## 6. Sơ đồ chân GPIO an toàn trên ESP32-S3 N16R8

> [!WARNING]
> Trên ESP32-S3 module N16R8, các chân **GPIO26 đến GPIO37** được kết nối trực tiếp với chip Flash và Octal PSRAM tốc độ cao bên trong IC. **Tuyệt đối không sử dụng GPIO26 đến GPIO37 cho nút nhấn, LED hoặc I2S**, nếu cấu hình vào dải chân này chip sẽ bị lỗi bus flash và reboot liên tục (`TG1WDT_SYS_RST`).

Cấu hình chân GPIO mặc định an toàn cho ESP32-S3:

| Chức năng | Chân GPIO | Ghi chú |
|---|:---:|---|
| **LED RGB WS2812** | **48** | Chân tín hiệu Data LED |
| **Mic I2S SCK / BCLK** | **5** | INMP441 Clock |
| **Mic I2S WS / LRC** | **4** | INMP441 Word Select |
| **Mic I2S SD / DIN** | **6** | INMP441 Serial Data |
| **DAC MAX98357 BCLK** | **15** | I2S Bit Clock Audio |
| **DAC MAX98357 LRC** | **16** | I2S Word Select / Left-Right |
| **DAC MAX98357 DIN** | **7** | I2S Data Out |
| **Nút nhấn Mic** | **41** | Chân nút nhấn bật/tắt Mic (nối GND) |
| **Nút Volume +** | **40** | Chân nút tăng âm lượng (nối GND) |
| **Nút Volume -** | **39** | Chân nút giảm âm lượng (nối GND) |
| **Nút WakeUP** | **42** | Chân nút đánh thức thủ công (nối GND) |
| **Rotary Encoder CLK** | **41** | Chân CLK biến trở xoay |
| **Rotary Encoder DT** | **40** | Chân DT biến trở xoay |
| **Rotary Encoder SW** | **42** | Chân nút bấm của biến trở xoay |
