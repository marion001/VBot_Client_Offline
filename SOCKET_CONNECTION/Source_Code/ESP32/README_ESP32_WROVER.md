# ESP32-WROVER

- Environment: `esp32-wrover`
- Board PlatformIO: `freenove_esp32_wrover`
- Flash: 4 MB theo bảng phân vùng hiện tại
- PSRAM: có
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
pio run -e esp32-wrover
pio run -e esp32-wrover -t buildfs
pio run -e esp32-wrover -t upload --upload-port COM11
pio run -e esp32-wrover -t uploadfs --upload-port COM11
```

Xem [README_FLASH.md](README_FLASH.md) để nạp thủ công.
