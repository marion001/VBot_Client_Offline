# ESP32 và ESP32 PSRAM/WROVER

## Nạp bằng Espressif Flash Download Tool

ESP32 Thông Tin Phân Vùng Dùng Để Nạp, Flash Chương Trình bằng Espressif Flash Download Tool:
```text
0x1000   bootloader.bin
0x8000   partitions.bin
0x10000  firmware.bin
0x350000  littlefs.bin
```

Các environment hỗ trợ:

| Phần cứng | Environment | Flash | LittleFS |
|---|---|---:|---:|
| ESP32 thường | `esp32` | 4 MB | `0x350000` |
| ESP32 có PSRAM | `esp32-psram` | 4 MB | `0x350000` |
| ESP32-WROVER | `esp32-wrover` | 4 MB | `0x350000` |
| ESP32 PSRAM, flash 8 MB | `esp32-psram-8mb` | 8 MB | `0x610000` |
| ESP32 PSRAM, flash 16 MB | `esp32-psram-16mb` | 16 MB | `0x810000` |

## GPIO mặc định

| Chức năng | GPIO |
|---|---:|
| LED WS2812 | 23 |
| Nút Mic / Volume+ / Volume- / WakeUp | 32 / 33 / 18 / 19 |
| Rotary CLK / DT / SW | 32 / 33 / 19 |
| Mic INMP441 SCK/BCLK / WS / SD | 14 / 13 / 34 |
| MAX98357 BCLK / LRC / DIN | 27 / 26 / 25 |

Các nút được đấu giữa GPIO và GND, firmware sử dụng `INPUT_PULLUP`.
