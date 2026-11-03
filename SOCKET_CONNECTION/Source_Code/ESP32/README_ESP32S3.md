# ESP32-S3 N8 không PSRAM

- Environment: `esp32s3`
- Board PlatformIO: `esp32-s3-devkitc-1`
- Flash: 8 MB
- Bảng phân vùng: `partitions_vbot_ota_8mb.csv`
- LittleFS: `0x610000`
- Bootloader ESP32-S3: `0x0000`

GPIO mặc định: mic WS/SCK/SD là `4/5/6`, MAX98357 DIN/BCLK/LRC là `7/15/16`, LED WS2812 là `48`.

Nút mặc định: WakeUp `GPIO42`, Volume- `GPIO39`, Volume+ `GPIO40`, Mic `GPIO41`.

```powershell
pio run -e esp32s3
pio run -e esp32s3 -t buildfs
pio run -e esp32s3 -t upload --upload-port COM11
pio run -e esp32s3 -t uploadfs --upload-port COM11
```

Không dùng firmware này cho bo cần PSRAM. Xem [README_FLASH.md](README_FLASH.md) để nạp thủ công.
