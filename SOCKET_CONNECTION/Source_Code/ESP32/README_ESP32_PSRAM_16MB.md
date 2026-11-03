# ESP32 PSRAM, flash 16 MB

- Environment: `esp32-psram-16mb`
- Flash: 16 MB
- PSRAM: có
- Bảng phân vùng: `partitions_vbot_ota_16mb.csv`
- Bootloader: `0x1000`
- LittleFS: `0x810000`

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
pio run -e esp32-psram-16mb
pio run -e esp32-psram-16mb -t buildfs
pio run -e esp32-psram-16mb -t upload --upload-port COM11
pio run -e esp32-psram-16mb -t uploadfs --upload-port COM11
```

Xem [README_FLASH.md](README_FLASH.md) để nạp thủ công.
