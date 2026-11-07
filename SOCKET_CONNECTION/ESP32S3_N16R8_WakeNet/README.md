# ESP32-S3 N16R8 WakeNet (On-Device Hotword)

Phiên bản đặc biệt dành cho board **ESP32-S3 N16R8** (16MB Flash, 8MB PSRAM OPI) tích hợp sẵn công nghệ nhận diện từ khóa đánh thức ngoại tuyến trực tiếp trên chip (**Espressif ESP-SR WakeNet9**).

- **Environment PlatformIO**: `esp32s3-n16r8`
- **Target**: `ESP32S3_N16R8_WakeNet`
- **Flash Size**: 16 MB, Flash Mode `QIO`
- **PSRAM**: 8 MB OPI (`qio_opi`)
- **Bảng phân vùng**: `partitions_vbot_ota_16mb.csv`
- **Tập hợp từ khóa mặc định tích hợp sẵn**: `Hi Lily`, `Alexa`, `Sophia`, `Jarvis`
- **Khả năng mở rộng**: Cho phép tải lên tối đa **5 từ khóa tùy chỉnh** (`.bin`) qua WebUI mà không cần nạp lại firmware.

---

## 1. Nạp ban đầu bằng Espressif Flash Download Tool

Mở phần mềm **Flash Download Tool** (chọn `ChipType: ESP32-S3`, `WorkMode: develop`):

| File nạp trong thư mục `bin/` | Địa chỉ Flash (Offset) | Ghi chú |
|---|:---:|---|
| `bootloader.bin` | `0x0000` | Bootloader ESP32-S3 (KHÔNG nạp tại 0x1000) |
| `partitions.bin` | `0x8000` | Bảng phân vùng 16MB OTA |
| `boot_app0.bin` | `0xe000` | Điều hướng boot partition OTA |
| `firmware.bin` | `0x10000` | Firmware chính VBot Client |
| `littlefs.bin` | `0x810000` | Giao diện WebUI & file âm thanh hệ thống (4MB) |
| `srmodels.bin` | `0xc00000` | Phân vùng mô hình WakeNet9 mặc định (4MB) |

**Cấu hình Flash:**
- **SPI SPEED**: `80MHz`
- **SPI MODE**: `QIO`
- **FLASH SIZE**: `128Mbit` hoặc `16MB` (theo dung lượng module)

---

## 2. Nạp ban đầu bằng `esptool.py`

```bash
esptool.py -p COM6 -b 921600 --before default_reset --after hard_reset --chip esp32s3 write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0000 bin/bootloader.bin \
  0x8000 bin/partitions.bin \
  0xe000 bin/boot_app0.bin \
  0x10000 bin/firmware.bin \
  0x810000 bin/littlefs.bin \
  0xc00000 bin/srmodels.bin
```

---

## 3. Cấu hình chân GPIO an toàn cho ESP32-S3 N16R8

> **LƯU Ý QUAN TRỌNG:** ESP32-S3 N16R8 sử dụng bộ nhớ Flash và PSRAM 8-line (OPI). Do đó các chân **GPIO26 đến GPIO37** được chip dùng nội bộ để giao tiếp bộ nhớ, **tuyệt đối không đấu nối ngoại vi vào các chân này**.

| Thiết bị / Ngoại vi | Chân tín hiệu | GPIO mặc định |
|---|---|:---:|
| **WS2812 LED** | DATA | `GPIO48` |
| **Nút nhấn thường** | Micro / Vol+ / Vol- / Wake | `41` / `40` / `39` / `42` |
| **Rotary Encoder** | CLK / DT / SW | `41` / `40` / `42` |
| **Micro I2S (INMP441)** | SCK (BCLK) / WS (LRC) / SD (DIN) | `5` / `4` / `6` |
| **Loa DAC (MAX98357A)** | BCLK / LRC / DIN | `15` / `16` / `7` |

---

## 4. Quản lý Hotword trên WebUI

- Truy cập tab **Cấu Hình Hotword WakeUP** trên WebUI của ESP32.
- Chọn chế độ: **WakeNet On-Device (ESP32-S3)**.
- Chọn các từ khóa có sẵn hoặc tải lên file `.bin` từ kho mẫu:  
  [ESP32S3_WakeNet_Hotword_Sample](https://github.com/marion001/VBot_Client_Offline/tree/main/SOCKET_CONNECTION/ESP32S3_WakeNet_Hotword_Sample)
