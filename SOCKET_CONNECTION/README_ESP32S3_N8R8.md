# ESP32-S3 N8R8

- Environment: `esp32s3-n8r8`
- Flash: 8 MB, chế độ QIO
- PSRAM: 8 MB OPI
- Memory type: `qio_opi`
- Bảng phân vùng: `partitions_vbot_ota_8mb.csv`
- LittleFS: `0x610000`
- Bootloader ESP32-S3: `0x0000`

GPIO mặc định: mic WS/SCK/SD là `4/5/6`, MAX98357 DIN/BCLK/LRC là `7/15/16`, LED WS2812 là `48`.

Nút mặc định: WakeUp `GPIO42`, Volume- `GPIO39`, Volume+ `GPIO40`, Mic `GPIO41`.

```powershell
pio run -e esp32s3-n8r8
pio run -e esp32s3-n8r8 -t buildfs
pio run -e esp32s3-n8r8 -t upload --upload-port COM11
pio run -e esp32s3-n8r8 -t uploadfs --upload-port COM11
```

Xem [README_FLASH.md](README_FLASH.md) để nạp thủ công và khắc phục lỗi boot.
