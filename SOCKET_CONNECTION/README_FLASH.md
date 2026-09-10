# Hướng dẫn build và flash firmware

## Chọn và nhập liệu đúng theo bảng sau khi nạp bằng Espressif Flash Download Tool

| Environment | Chip | Flash | PSRAM | bootloader.bin | partitions.bin | firmware.bin | littlefs.bin |
|---|---|---:|---:|---:|---:|---:|---:|
| `esp32` | ESP32 | 4 MB | Không | `0x1000` | `0x8000` | `0x10000` | `0x350000` |
| `esp32-psram` | ESP32 | 4 MB | Có | `0x1000` | `0x8000` | `0x10000` | `0x350000` |
| `esp32-wrover` | ESP32-WROVER | 4 MB | Có | `0x1000` | `0x8000` | `0x10000` | `0x350000` |
| `esp32-psram-8mb` | ESP32 | 8 MB | Có | `0x1000` | `0x8000` | `0x10000` | `0x610000` |
| `esp32-psram-16mb` | ESP32 | 16 MB | Có | `0x1000` | `0x8000` | `0x10000` | `0x810000` |
| `esp32s3` | ESP32-S3 | 8 MB | Không | `0x0000` | `0x8000` | `0x10000` | `0x610000` |
| `esp32s3-n8r2` | ESP32-S3 | 8 MB | 2 MB QSPI | `0x0000` | `0x8000` | `0x10000` | `0x610000` |
| `esp32s3-n8r8` | ESP32-S3 | 8 MB | 8 MB OPI | `0x0000` | `0x8000` | `0x10000` | `0x610000` |
| `esp32s3-n16r8` | ESP32-S3 | 16 MB | 8 MB OPI | `0x0000` | `0x8000` | `0x10000` | `0x810000` |

## GPIO mặc định theo dòng chip

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

Các nút được đấu giữa GPIO tương ứng và GND; firmware sử dụng `INPUT_PULLUP`. Không dùng GPIO22–37 cho ngoại vi trên ESP32-S3 N8R8/N16R8 vì các chân khả dụng trong vùng này có thể thuộc bus flash/OPI PSRAM.

## Nạp bằng Espressif Flash Download Tool

Xóa toàn bộ flash trước khi đổi loại firmware hoặc khi gặp `Invalid image block`.

ESP32:

```text
0x1000   bootloader.bin
0x8000   partitions.bin
0x10000  firmware.bin
<offset LittleFS trong bảng>  littlefs.bin
```

ESP32-S3:

```text
0x0000   bootloader.bin
0x8000   partitions.bin
0x10000  firmware.bin
<offset LittleFS trong bảng>  littlefs.bin
```

Bộ firmware của dự án gồm đúng bốn file ở trên. Vùng `otadata` tại `0xE000` được để trống khi nạp lần đầu; bootloader sẽ khởi động ứng dụng tại phân vùng `ota_0`. Không trộn file giữa các environment hoặc giữa ESP32 và ESP32-S3.

Trong Flash Download Tool, chọn đúng chip, flash size và flash mode. Các cấu hình S3 có PSRAM trong dự án dùng `QIO` cho flash; N8R8/N16R8 dùng OPI PSRAM.

## Khắc phục lỗi boot

Nếu log có `Invalid image block, can't boot`:

1. Xác nhận đúng chip và environment.
2. Xóa toàn bộ flash.
3. Kiểm tra offset bootloader: ESP32 là `0x1000`, ESP32-S3 là `0x0000`.
4. Build lại toàn bộ file từ cùng một environment.
5. Giảm tốc độ upload nếu kết nối không ổn định.

`FramingError` xuất hiện trong lúc bo reset thường chỉ là hệ quả của vòng lặp khởi động.
