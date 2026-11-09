# ESP32 có PSRAM

- Environment: `esp32-psram`
- Flash: 4 MB
- PSRAM: có, bật bằng `BOARD_HAS_PSRAM`
- Bảng phân vùng: `partitions_vbot_ota.csv`
- Bootloader: `0x1000`
- LittleFS: `0x350000`

## GPIO mặc định

| Chức năng | GPIO |
|---|---:|
| LED WS2812 | 23 |
| Nút Mic / Volume+ / Volume- / WakeUp | 32 / 33 / 18 / 19 |
| Rotary CLK / DT / SW | 32 / 33 / 19 |
| Mic INMP441 SCK/BCLK / WS / SD | 14 / 13 / 34 |
| MAX98357 BCLK / LRC / DIN | 27 / 26 / 25 |

Các nút được đấu giữa GPIO và GND, firmware sử dụng `INPUT_PULLUP`.

```powershell
pio run -e esp32-psram
pio run -e esp32-psram -t buildfs
pio run -e esp32-psram -t upload --upload-port COM11
pio run -e esp32-psram -t uploadfs --upload-port COM11
```

Xem [README_FLASH.md](README_FLASH.md) để nạp thủ công.
