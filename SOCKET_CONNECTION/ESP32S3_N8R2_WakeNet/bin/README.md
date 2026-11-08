# ESP32-S3 N8R2 WakeNet (On-Device Hotword)

Phiên bản đặc biệt dành cho board **ESP32-S3 8MB Flash** có 2MB PSRAM QSPI (Quad SPI) tích hợp sẵn công nghệ nhận diện từ khóa đánh thức ngoại tuyến trực tiếp trên chip (**Espressif ESP-SR WakeNet9**).

- **Environment PlatformIO**: `esp32s3-n8r2-wakenet`
- **Target**: `ESP32S3_N8R2_WakeNet`
- **Flash Size**: 8 MB, Flash Mode `QIO`
- **PSRAM**: 2MB PSRAM QSPI (Quad SPI) (`qio_qspi`)
- **Bảng phân vùng**: `partitions_vbot_ota_8mb_wakenet.csv`
- **Tập hợp từ khóa mặc định tích hợp sẵn**: `Hi Lily`, `Alexa`, `Sophia`, `Jarvis`
- **Khả năng mở rộng**: Cho phép tải lên tối đa **5 từ khóa tùy chỉnh** (`.bin`) qua WebUI mà không cần nạp lại firmware.

---

## 1. Nạp ban đầu bằng Espressif Flash Download Tool

Mở phần mềm **Flash Download Tool** (chọn `ChipType: ESP32-S3`, `WorkMode: develop`):

| File nạp trong thư mục `bin/` | Địa chỉ Flash (Offset) | Ghi chú |
|---|:---:|---|
| `bootloader.bin` | `0x0000` | Bootloader ESP32-S3 (KHÔNG nạp tại 0x1000) |
| `partitions.bin` | `0x8000` | Bảng phân vùng 8MB WakeNet |
| `boot_app0.bin` | `0xe000` | Điều hướng boot partition OTA |
| `firmware.bin` | `0x10000` | Firmware chính VBot Client |
| `littlefs.bin` | `0x490000` | Giao diện WebUI & file âm thanh hệ thống (1.875MB) |
| `srmodels.bin` | `0x670000` | Phân vùng mô hình WakeNet9 mặc định (1.5MB) |

**Cấu hình Flash:**
- **SPI SPEED**: `80MHz`
- **SPI MODE**: `QIO`
- **FLASH SIZE**: `64Mbit` hoặc `8MB` (theo dung lượng module)

---

## 2. Nạp ban đầu bằng `esptool.py`

```bash
esptool.py -p COM_PORT -b 921600 --before default_reset --after hard_reset --chip esp32s3 write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m \
  0x0000 bin/bootloader.bin \
  0x8000 bin/partitions.bin \
  0xe000 bin/boot_app0.bin \
  0x10000 bin/firmware.bin \
  0x490000 bin/littlefs.bin \
  0x670000 bin/srmodels.bin
```

---

## 3. Cập nhật OTA qua WebUI

Sau khi nạp lần đầu, các lần nâng cấp tiếp theo có thể thực hiện hoàn toàn qua WebUI (`/update`):
- **Firmware**: Chọn file `firmware.bin`
- **Filesystem**: Chọn file `littlefs.bin`
- **Mô hình WakeNet**: Quản lý và tải file mô hình tùy chỉnh tại mục **Cấu hình Hotword / WakeNet** trên WebUI.
