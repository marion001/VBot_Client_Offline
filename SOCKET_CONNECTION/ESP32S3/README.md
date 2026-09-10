# ESP32-S3 N8 không PSRAM

## Nạp bằng Espressif Flash Download Tool

ESP32 Thông Tin Phân Vùng Dùng Để Nạp, Flash Chương Trình bằng Espressif Flash Download Tool:
```text
0x0000   bootloader.bin
0x8000   partitions.bin
0x10000  firmware.bin
0x610000  littlefs.bin
```

- Environment: `esp32s3`
- Board PlatformIO: `esp32-s3-devkitc-1`
- Flash: 8 MB
- Bảng phân vùng: `partitions_vbot_ota_8mb.csv`
- LittleFS: `0x610000`
- Bootloader ESP32-S3: `0x0000`

## GPIO mặc định
GPIO mặc định: mic WS/SCK/SD là `4/5/6`, MAX98357 DIN/BCLK/LRC là `7/15/16`, LED WS2812 là `48`.

| Chức năng | GPIO |
|---|---:|
| LED WS2812 | 48 |
| Nút Mic / Volume+ / Volume- / WakeUp | 41 / 40 / 39 / 42 |
| Mic INMP441 SCK/BCLK / WS / SD | 5 / 4 / 6 |
| MAX98357 BCLK / LRC / DIN | 15 / 16 / 7 |

Nút mặc định: WakeUp `GPIO42`, Volume- `GPIO39`, Volume+ `GPIO40`, Mic `GPIO41`.
