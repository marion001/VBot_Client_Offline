# ESP32 WakeNet (On-Device Hotword)

Phiên bản dành cho chip **ESP32** (Xtensa LX6 hai nhân có PSRAM ngoài và Flash 16MB, ví dụ: ESP32-WROVER, ESP32-WROVER-E/IB hoặc ESP32 DevKit 16MB Flash + PSRAM) tích hợp sẵn công nghệ nhận diện từ khóa đánh thức ngoại tuyến trực tiếp trên chip (**Espressif ESP-SR WakeNet9**).

- **Environment PlatformIO**: `esp32-wakenet` (hoặc `esp32-wrover-wakenet`)
- **Target**: `ESP32_WakeNet`
- **Flash Size**: 16 MB, Flash Mode `DIO` / `QIO`
- **Yêu cầu phần cứng**: Bắt buộc chip ESP32 có **PSRAM** (tối thiểu 4MB/8MB PSRAM) để chứa trọng số mô hình nơ-ron mạng và bộ đệm FFT.
- **Bảng phân vùng**: `partitions_vbot_ota_16mb_wakenet.csv`
- **Tập hợp từ khóa mặc định tích hợp sẵn**: `Hi Lily`, `Alexa`, `Sophia`, `Jarvis`
- **Khả năng mở rộng**: Cho phép tải lên tối đa **5 từ khóa tùy chỉnh** (`.bin`) qua WebUI mà không cần nạp lại firmware.

---

## 1. Nạp ban đầu bằng Espressif Flash Download Tool

Mở phần mềm **Flash Download Tool** (chọn `ChipType: ESP32`, `WorkMode: develop`):

| File nạp trong thư mục `bin/` | Địa chỉ Flash (Offset) | Ghi chú |
|---|:---:|---|
| `bootloader.bin` | `0x1000` | Bootloader ESP32 chuẩn (bắt đầu tại 0x1000) |
| `partitions.bin` | `0x8000` | Bảng phân vùng 16MB OTA |
| `boot_app0.bin` | `0xe000` | Điều hướng boot partition OTA |
| `firmware.bin` | `0x10000` | Firmware chính VBot Client |
| `littlefs.bin` | `0x810000` | Giao diện WebUI & file âm thanh hệ thống (4MB) |
| `srmodels.bin` | `0xc00000` | Phân vùng mô hình WakeNet9 mặc định (4MB) |

**Cấu hình Flash:**
- **SPI SPEED**: `40MHz` hoặc `80MHz`
- **SPI MODE**: `DIO` (khuyến nghị cho ESP32 WROVER để tránh xung đột chân PSRAM)
- **FLASH SIZE**: `128Mbit` hoặc `16MB`

---

## 2. Nạp ban đầu bằng `esptool.py`

```bash
esptool.py -p COM11 -b 921600 --before default_reset --after hard_reset --chip esp32 write_flash --flash_mode dio --flash_size 16MB --flash_freq 40m \
  0x1000 bin/bootloader.bin \
  0x8000 bin/partitions.bin \
  0xe000 bin/boot_app0.bin \
  0x10000 bin/firmware.bin \
  0x810000 bin/littlefs.bin \
  0xc00000 bin/srmodels.bin
```

---

## 3. Cấu hình chân GPIO an toàn cho ESP32 WROVER / PSRAM

> **LƯU Ý QUAN TRỌNG:** ESP32 sử dụng PSRAM qua các chân GPIO16 và GPIO17 (đối với hầu hết các module WROVER). Do đó **tuyệt đối không đấu nối ngoại vi vào GPIO16 và GPIO17**, cũng như các chân Flash SPI nội bộ (GPIO6 -> GPIO11).

| Thiết bị / Ngoại vi | Chân tín hiệu | GPIO mặc định khuyến nghị |
|---|---|:---:|
| **WS2812 LED** | DATA | `GPIO22` |
| **Nút nhấn thường** | Micro / Vol+ / Vol- / Wake | `32` / `33` / `25` / `21` |
| **Micro I2S (INMP441)** | SCK (BCLK) / WS (LRC) / SD (DIN) | `14` / `13` / `34` |
| **Loa DAC (MAX98357A)** | BCLK / LRC / DIN | `26` / `27` / `19` |

---

## 4. Quản lý Hotword trên WebUI

- Truy cập tab **Cấu Hình Hotword WakeUP** trên WebUI của ESP32.
- Chọn chế độ: **WakeNet On-Device**.
- Chọn các từ khóa có sẵn hoặc tải lên file `.bin` từ kho mẫu:  
  [ESP_WakeNet_Hotword_Sample](https://github.com/marion001/VBot_Client_Offline/tree/main/SOCKET_CONNECTION/ESP_WakeNet_Hotword_Sample)
