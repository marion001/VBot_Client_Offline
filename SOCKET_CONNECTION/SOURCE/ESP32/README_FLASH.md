# Hướng dẫn build và flash firmware

## 1. Chọn đúng environment

| Environment | Chip | Flash | PSRAM | LittleFS | Phân vùng WakeNet9 Model |
|---|---|---:|---:|---:|:---:|
| `esp32` | ESP32 | 4 MB | Không | `0x350000` | - |
| `esp32-psram` | ESP32 | 4 MB | Có | `0x350000` | - |
| `esp32-wrover` | ESP32-WROVER | 4 MB | Có | `0x350000` | - |
| `esp32-psram-8mb` | ESP32 | 8 MB | Có | `0x610000` | - |
| `esp32-psram-16mb` | ESP32 | 16 MB | Có | `0x810000` | - |
| `esp32-wakenet` | ESP32 | 16 MB | Có | `0x810000` | `0xC00000` (`srmodels.bin`) |
| `esp32-wrover-wakenet` | ESP32-WROVER | 16 MB | Có | `0x810000` | `0xC00000` (`srmodels.bin`) |
| `esp32s3` | ESP32-S3 | 8 MB | Không | `0x610000` | - |
| `esp32s3-n8r2` | ESP32-S3 | 8 MB | 2 MB QSPI | `0x610000` | - |
| `esp32s3-n8r2-wakenet` | ESP32-S3 | 8 MB | 2 MB QSPI | `0x490000` | `0x670000` (`srmodels.bin`) |
| `esp32s3-n8r8` | ESP32-S3 | 8 MB | 8 MB OPI | `0x610000` | - |
| `esp32s3-n8r8-wakenet` | ESP32-S3 | 8 MB | 8 MB OPI | `0x490000` | `0x670000` (`srmodels.bin`) |
| `esp32s3-n16r8` | ESP32-S3 | 16 MB | 8 MB OPI | `0x810000` | - |
| `esp32s3-n16r8-wakenet` | ESP32-S3 | 16 MB | 8 MB OPI | `0x810000` | `0xC00000` (`srmodels.bin`) |

---

## 2. GPIO mặc định theo dòng chip

| Chức năng | ESP32 | ESP32-S3 |
|---|---:|---:|
| LED WS2812 | 23 | 48 |
| Nút Mic | 32 | 41 |
| Nút Volume+ | 33 | 40 |
| Nút Volume- | 18 | 39 |
| Nút WakeUp | 19 | 42 |
| Rotary CLK / DT / SW | 32 / 33 / 19 | 41 / 40 / 42 |
| Mic SCK/BCLK / WS / SD | 14 / 13 / 34 | 5 / 4 / 6 |
| MAX98357 BCLK / LRC / DIN | 27 / 26 / 25 | 15 / 16 / 7 |

Các nút được đấu giữa GPIO tương ứng và GND; firmware sử dụng `INPUT_PULLUP`. Không dùng GPIO26–37 cho ngoại vi trên ESP32-S3 N8R8/N16R8 vì các chân này thuộc bus Flash và Octal PSRAM tốc độ cao nội bộ.

---

## 3. Build bằng PlatformIO

Thay `<env>` bằng environment trong bảng (ví dụ: `esp32s3-n8r2-wakenet`, `esp32s3-n8r8-wakenet`, `esp32s3-n16r8-wakenet`):

```powershell
pio run -e <env>
pio run -e <env> -t buildfs
```

Các file kết quả nằm trong `.pio/build/<env>/`.

---

## 4. Nạp trực tiếp bằng PlatformIO

Đây là cách được khuyến nghị vì PlatformIO tự chọn đúng offset:

```powershell
pio run -e <env> -t erase --upload-port COM6
pio run -e <env> -t upload --upload-port COM6
pio run -e <env> -t uploadfs --upload-port COM6
```

Đổi `COM6` thành cổng COM thực tế.

Đối với dòng có **WakeNet**, nạp thêm file mô hình WakeNet9 mặc định vào phân vùng tương ứng:
- Cho **ESP32-S3 8MB WakeNet** (`esp32s3-n8r2-wakenet`, `esp32s3-n8r8-wakenet`):
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py" --chip esp32s3 --port COM6 write_flash 0x670000 models/srmodels.bin
```
- Cho **ESP32-S3 16MB WakeNet** (`esp32s3-n16r8-wakenet`):
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py" --chip esp32s3 --port COM6 write_flash 0xC00000 models/srmodels.bin
```

---

## 5. Nạp bằng Espressif Flash Download Tool

Xóa toàn bộ flash trước khi đổi loại firmware hoặc khi gặp `Invalid image block`.

### Cấu hình cho ESP32 thường (Flash 4MB):
```text
0x1000   bootloader.bin
0x8000   partitions.bin
0x10000  firmware.bin
0x350000 littlefs.bin
```

### Cấu hình cho ESP32-S3 N8 / N8R2 / N8R8 (Bản chuẩn không WakeNet, Flash 8MB):
```text
0x0000   bootloader.bin
0x8000   partitions.bin
0x10000  firmware.bin
0x610000 littlefs.bin
```

### Cấu hình cho ESP32-S3 N8R2 / N8R8 WakeNet (Flash 8MB, PSRAM QSPI/OPI):
```text
0x0000   bootloader.bin
0x8000   partitions.bin
0xe000   boot_app0.bin
0x10000  firmware.bin
0x490000 littlefs.bin
0x670000 models/srmodels.bin
```

### Cấu hình cho ESP32-S3 N16R8 WakeNet (Flash 16MB, PSRAM 8MB OPI):
```text
0x0000    bootloader.bin
0x8000    partitions.bin
0xe000    boot_app0.bin
0x10000   firmware.bin
0x810000  littlefs.bin
0xC00000  models/srmodels.bin
```

> [!IMPORTANT]
> - Bootloader ESP32 dùng offset `0x1000`.
> - Bootloader ESP32-S3 dùng offset `0x0000` (Không ghi bootloader S3 tại `0x1000`).
> - Phân vùng `srmodels.bin`: `0x670000` cho bản 8MB Flash WakeNet, `0xC00000` cho bản 16MB Flash WakeNet.

---

## 6. Khắc phục lỗi boot

Nếu log có `Invalid image block, can't boot`:
1. Xác nhận đúng chip và environment.
2. Xóa toàn bộ flash trước khi nạp (`pio run -e <env> -t erase`).
3. Kiểm tra offset bootloader: ESP32 là `0x1000`, ESP32-S3 là `0x0000`.
4. Build lại toàn bộ file từ cùng một environment.
5. Giảm tốc độ upload (baud rate) nếu kết nối chập chờn.
