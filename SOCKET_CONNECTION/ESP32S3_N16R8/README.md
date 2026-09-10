# ESP32-S3 N16R8

- Environment: `esp32s3-n16r8`
- Flash: 16 MB, chế độ QIO
- PSRAM: 8 MB OPI
- Memory type: `qio_opi`
- Bảng phân vùng: `partitions_vbot_ota_16mb.csv`
- LittleFS: `0x810000`
- Bootloader ESP32-S3: `0x0000`

## Nạp bằng Espressif Flash Download Tool

```text
0x0000    bootloader.bin
0x8000    partitions.bin
0x10000   firmware.bin
0x810000  littlefs.bin
```

Chọn chip `ESP32-S3`, flash size `16MB` và flash mode `QIO`. Hãy xóa toàn bộ flash trước khi sửa lỗi `Invalid image block`. Không ghi bootloader tại `0x1000`.

Nếu log báo `rst:0x8 (TG1WDT_SYS_RST)` ngay sau khi Wi-Fi kết nối, hãy dùng bản firmware mới nhất. Task FastLED của ESP32-S3 đã được tăng stack và chuyển xuống cuối quá trình khởi tạo để tránh panic/double exception trên core 0.

ESP32-S3 không có GPIO22 đến GPIO25. Trên module N16R8, GPIO26 đến GPIO37 còn được flash/OPI PSRAM sử dụng và không được dùng cho nút hoặc I2S. Firmware tự chuyển cấu hình ESP32 cũ sang bộ chân S3 an toàn:

| Chức năng | GPIO |
|---|---:|
| LED WS2812 | 48 |
| Nút Mic / Volume+ / Volume- / Wake | 41 / 40 / 39 / 42 |
| Rotary CLK / DT / SW | 41 / 40 / 42 |
| Mic SCK/BCLK / WS / SD | 5 / 4 / 6 |
| MAX98357 BCLK / LRC / DIN | 15 / 16 / 7 |
