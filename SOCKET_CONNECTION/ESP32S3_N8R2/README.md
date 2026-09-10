# ESP32-S3 N8R2

## Nạp bằng Espressif Flash Download Tool

ESP32 Thông Tin Phân Vùng Dùng Để Nạp, Flash Chương Trình bằng Espressif Flash Download Tool:
```text
0x0000   bootloader.bin
0x8000   partitions.bin
0x10000  firmware.bin
0x610000  littlefs.bin
```

- Environment: `esp32s3-n8r2`
- Flash: 8 MB, chế độ QIO
- PSRAM: 2 MB QSPI
- Bảng phân vùng: `partitions_vbot_ota_8mb.csv`
- LittleFS: `0x610000`
- Bootloader ESP32-S3: `0x0000`

## GPIO mặc định
GPIO mặc định: mic WS/SCK/SD là `4/5/6`, MAX98357 DIN/BCLK/LRC là `7/15/16`, LED WS2812 là `48`.

Nút mặc định: WakeUp `GPIO42`, Volume- `GPIO39`, Volume+ `GPIO40`, Mic `GPIO41`.
| Chức năng | GPIO |
|---|---:|
| LED WS2812 | 48 |
| Nút Mic / Volume+ / Volume- / WakeUp | 41 / 40 / 39 / 42 |
| Rotary CLK / DT / SW | 32 / 33 / 19 |
| Mic INMP441 SCK/BCLK / WS / SD | 5 / 4 / 6 |
| MAX98357 BCLK / LRC / DIN | 15 / 16 / 7 |


<img width="468" height="743" alt="Image" src="https://github.com/user-attachments/assets/289ec094-edcc-4819-a648-43f4e0aa1751" />
