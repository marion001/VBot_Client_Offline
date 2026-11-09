# ESP32-S3 N8R8

Bo mạch ESP32-S3 với 8MB Flash và 8MB PSRAM OPI (`qio_opi`).

## 1. Bản chuẩn (Standard - Không WakeNet On-Device)
- **Environment**: `esp32s3-n8r8`
- **Flash**: 8 MB, chế độ QIO
- **PSRAM**: 8 MB OPI
- **Bảng phân vùng**: `partitions_vbot_ota_8mb.csv`
- **LittleFS**: `0x610000` (1.875 MB)
- **Bootloader ESP32-S3**: `0x0000`

```powershell
pio run -e esp32s3-n8r8
pio run -e esp32s3-n8r8 -t buildfs
pio run -e esp32s3-n8r8 -t upload --upload-port COM11
pio run -e esp32s3-n8r8 -t uploadfs --upload-port COM11
```

## 2. Bản WakeNet (Tích hợp AI On-Device Hotword)
- **Environment**: `esp32s3-n8r8-wakenet`
- **Flash**: 8 MB, chế độ QIO
- **PSRAM**: 8 MB OPI
- **Bảng phân vùng**: `partitions_vbot_ota_8mb_wakenet.csv`
- **LittleFS**: `0x490000` (1.875 MB)
- **Mô hình WakeNet9**: `0x670000` (1.5 MB - nạp file `models/srmodels.bin`)
- **Bootloader ESP32-S3**: `0x0000`

```powershell
pio run -e esp32s3-n8r8-wakenet
pio run -e esp32s3-n8r8-wakenet -t buildfs
pio run -e esp32s3-n8r8-wakenet -t upload --upload-port COM11
pio run -e esp32s3-n8r8-wakenet -t uploadfs --upload-port COM11
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py" --chip esp32s3 --port COM11 write_flash 0x670000 models/srmodels.bin
```

---

GPIO mặc định: mic WS/SCK/SD là `4/5/6`, MAX98357 DIN/BCLK/LRC là `7/15/16`, LED WS2812 là `48`.

Nút mặc định: WakeUp `GPIO42`, Volume- `GPIO39`, Volume+ `GPIO40`, Mic `GPIO41`.

> [!NOTE]
> Không dùng GPIO26–37 cho ngoại vi vì các chân này thuộc bus Octal PSRAM/Flash nội bộ.

Xem [README_FLASH.md](README_FLASH.md) để nạp thủ công bằng Flash Download Tool và khắc phục lỗi boot.
