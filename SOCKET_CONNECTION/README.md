# ESP32 VBot Socket Client

Lưu ý quan trọng: bootloader ESP32 dùng offset `0x1000`, còn bootloader ESP32-S3 dùng offset `0x0000`. Không dùng chung offset giữa hai dòng chip.

WebUI có thể chọn khởi chạy kết nối máy chủ VBot hoặc chạy độc lập. Chế độ độc lập không khởi tạo Mic/WebSocket; MQTT, mDNS, phát âm thanh, nút nhấn, LED, WebUI và OTA vẫn hoạt động.

Client ESP32 cho `Streaming.py` khi server đặt `connection_protocol = "socket"`.

- [Hướng dẫn flash fimware](README_FLASH.md)

## Chọn và nhập liệu đúng theo bảng sau khi nạp bằng Espressif Flash Download Tool

| Environment | Chip | Flash | PSRAM | bootloader.bin | partitions.bin | firmware.bin | littlefs.bin |
|---|---|---:|---:|---:|---:|---:|---:|
| `esp32` | ESP32 | 4 MB | Không | `0x1000` | `0x8000` | `0x10000` | `0x350000` |
| `esp32-psram` | ESP32 | 4 MB | Có | `0x1000` | `0x8000` | `0x10000` | `0x350000` |
| `esp32-wrover` | ESP32-WROVER | 4 MB | Có | `0x1000` | `0x8000` | `0x10000` | `0x350000` |
| `esp32-psram-8mb` | ESP32 | 8 MB | Có | `0x1000` | `0x8000` | `0x10000` | `0x610000` |
| `esp32-psram-16mb` | ESP32 | 16 MB | Có | `0x1000` | `0x8000` | `0x10000` | `0x810000` |
| `esp32s3` | ESP32-S3 | 8 MB | Không | `0x0000` | `0x8000` | `0x10000` | `0x610000` |
| `esp32s3-n8r2` | ESP32-S3 | 8 MB | 2 MB QSPI | `0x0000` | `0x8000` | `0x10000` | `0x610000` |
| `esp32s3-n8r8` | ESP32-S3 | 8 MB | 8 MB OPI | `0x0000` | `0x8000` | `0x10000` | `0x610000` |
| `esp32s3-n16r8` | ESP32-S3 | 16 MB | 8 MB OPI | `0x0000` | `0x8000` | `0x10000` | `0x810000` |

## Tính năng

- WiFiManager captive portal, tự reconnect WiFi và mở lại AP cấu hình khi mất WiFi.
- WebUI chạy bằng ESPAsyncWebServer, cấu hình GPIO, WebSocket server, working mode, âm lượng, gain mic, log serial, LED WS2812, I2S mic INMP441, I2S DAC/MAX98357.
- INMP441 có thể chọn kênh mic Left/Right trong WebUI, mặc định dùng Left (L).
- WebUI hiển thị SSID WiFi đang kết nối và thông tin chip ESP32.
- WebUI có cấu hình bật/tắt tự động gửi mic để server đánh thức hotword. Khi tắt, client chỉ gửi mic sau khi bấm WakeUP/start recording.
- Lưu cấu hình bằng NVS `Preferences`.
- OTA firmware bằng ElegantOTA tại `/update`.
- API JSON trạng thái/cấu hình tại `/VBot_Client_Info`.
- API check PSRAM tại `/Check_PS_RAM`, trả `psram_active`, `psram_capacity_mb`, `chip_suffix`.
- Tải cấu hình NVS JSON tại `/download_config`, khôi phục cấu hình bằng upload JSON tại `/restore_config`.
- Test hiệu ứng LED bằng HTTP POST `/test_led` với form field `effect`, ví dụ `LED_SPEAK`.
- WebSocket protocol theo `README_Socket.md`.
- Gửi mic PCM raw signed 16-bit little-endian mono 16 kHz, frame 512 samples.
- Phát MP3 URL từ `status_audio` / `tts_audio`.
- Phát Google Translate TTS tiếng Việt từ WebUI hoặc MQTT
  `<mqtt_client>/script/vbot_tts/set`; URL HTTPS luôn được chuyển qua
  `audio_proxy` HTTP đang cấu hình.
- Điều chỉnh hệ số PCM RAW trong WebUI: `1.0` giữ nguyên, nhỏ hơn `1.0` làm
  nhỏ âm thanh, lớn hơn `1.0` khuếch đại; mặc định khi cấu hình mới là `1.0`.
- Hỗ trợ cấu hình `audio_proxy` nội bộ trong WebUI để phát các URL HTTPS, YouTube, ZingMP3 hoặc link cần resolve trước khi ESP32 phát.
- Phát PCM raw realtime từ cặp message `pcm_raw_audio` metadata + binary.
- 4 nút: Mic, Volume +, Volume -, WakeUP.
- Hiệu ứng LED: `LED_SPEAK`, `LED_THINK`, `LED_LOADING`, `LED_MUTE`, `LED_ERROR`, `LED_STARTUP`, `LED_PAUSE`, `LED_VOLUME`, `LED_OFF`.
- FreeRTOS task riêng cho mic, WebSocket, DAC/audio, LED, button và WiFi reconnect.

## Sơ đồ cấu trúc file

```text
esp32_vbot_client/
├─ platformio.ini
├─ partitions_vbot_ota.csv
├─ README.md
├─ audio_proxy_test_server.py
├─ audio_proxy_test_requirements.txt
├─ src/
│  └─ main.cpp
├─ scripts/
│  └─ gzip_webui.py
├─ data/
│  ├─ index.html
│  ├─ app.css
│  ├─ app.js
│  ├─ busy.html
│  └─ sound/
│     ├─ ding.mp3
│     ├─ dong.mp3
│     ├─ mic_off.mp3
│     ├─ mic_on.mp3
│     ├─ music_stops.mp3
│     ├─ start_up.mp3
│     └─ tut_tut.mp3
└─ .pio/
   ├─ littlefs_data/
   │  ├─ index.html.gz
   │  ├─ app.css.gz
   │  ├─ app.js.gz
   │  ├─ busy.html.gz
   │  └─ sound/
   └─ build/
      └─ esp32/
         ├─ firmware.bin
         └─ littlefs.bin
```

## File gốc cần chỉnh sửa

Chỉ sửa các file gốc trong thư mục `data/` và `src/`.

WebUI:

```text
data/index.html
data/app.css
data/app.js
data/busy.html
```

Âm thanh lưu trong LittleFS:

```text
data/sound/*.mp3
```

Firmware ESP32:

```text
src/main.cpp
```

Không sửa trực tiếp các file trong `.pio/littlefs_data/` hoặc `.pio/build/` vì đây là file build tự sinh.

Thư mục `$PROJECT_DATA_DIR` nếu còn tồn tại trong project chỉ là thư mục cũ/không dùng trong cấu hình hiện tại. Theo `platformio.ini`, thư mục nguồn đúng là `data/`, thư mục staging đúng là `.pio/littlefs_data/`.

## Cơ chế `audio_proxy`

ESP32 không phát trực tiếp ổn định các URL `https://...`, YouTube, ZingMP3 hoặc các trang nhạc cần bóc link stream thật. Vì vậy WebUI có thêm cấu hình:

```text
URL API stream audio_proxy
Sử dụng URL API stream đã nhập
```

URL proxy này phải là server nội bộ dùng `http://`, không dùng `https://`. Ví dụ:

```text
http://192.168.1.20:5000/audio_proxy?url=
```

Hoặc chỉ nhập base URL:

```text
http://192.168.1.20:5000
```

Khi checkbox được bật, firmware sẽ tự đổi các URL cần xử lý thành:

```text
http://192.168.1.20:5000/audio_proxy?url=<URL_goc_da_encode>
```

Quy tắc xử lý URL:

- URL âm thanh `https://...` luôn đi qua `audio_proxy` vì ESP32 client không xử lý HTTPS trực tiếp.
- URL YouTube, ZingMP3 hoặc URL web nhạc người dùng nhập trong ô Play URL sẽ đi qua `audio_proxy`.
- URL `/audio_proxy?id=...` hoặc `/audio_proxy?url=...` từ server VBot cũng có thể được chuyển qua proxy nội bộ đã nhập.

### Google Translate TTS

WebUI có ô **Google Translate TTS** trong phần thao tác âm thanh. Firmware tạo
URL dạng:

```text
https://translate.google.com/translate_tts?ie=UTF-8&tl=vi-VN&client=tw-ob&ttsspeed=1.0&q=...
```

ESP32 không mở URL HTTPS này trực tiếp mà gửi nó qua **URL API stream
audio_proxy**. Nội dung tối đa 240 byte UTF-8. Home Assistant có thể gửi cùng
nội dung qua topic:

```text
<Tên_Client_MQTT>/script/vbot_tts/set
```

Trạng thái được publish tại:

```text
<Tên_Client_MQTT>/tts/state
```

### API phát URL âm thanh

Gửi POST tới ESP32:

```text
POST http://<IP_ESP32>/api/play_url?url=http://192.168.1.10/music/test.mp3
```

Hoặc dùng form field `url`/`audio_url`. File âm thanh HTTP có đuôi `.mp3`,
`.m4a`, `.aac`, `.ogg` hoặc `.wav` được phát trực tiếp, phù hợp với máy chủ
file trong mạng LAN. URL HTTPS và URL web cần phân giải vẫn được chuyển qua
`audio_proxy`.

Home Assistant tạo hai entity cho profile ESP32:

```text
text.vbot_play_music_link_url_<device>
button.media_play_link_url_button_<device>
```

Nhập URL vào Text rồi nhấn Button để phát.
- URL âm thanh nội bộ từ VBot server dạng `http://<ip_server>/...mp3`, `http://<ip_server>/assets/sound/...` sẽ phát trực tiếp, không đi qua proxy.
- Nếu không tích checkbox, firmware giữ nguyên hành vi cũ và phát URL server trả về trực tiếp.

Server mẫu để test nằm trong:

```text
audio_proxy_test_server.py
audio_proxy_test_requirements.txt
```

Cách chạy server mẫu trên máy tính nội bộ:

```powershell
cd C:\Users\PC-Tuyen\Desktop\VBot_ToiUu\esp32_vbot_client
python -m pip install -r audio_proxy_test_requirements.txt
python audio_proxy_test_server.py --host 0.0.0.0 --port 5000
```

Các endpoint test:

```text
GET /health
GET /resolve?url=<youtube_zingmp3_hoac_audio_url>
GET /audio_proxy?url=<youtube_zingmp3_hoac_audio_url>
GET /register?url=<youtube_zingmp3_hoac_audio_url>
```

Trong WebUI ESP32, nhập:

```text
http://<IP_may_chay_proxy>:5000/audio_proxy?url=
```

Sau đó tích `Sử dụng URL API stream đã nhập`, lưu cấu hình và khởi động lại ESP32.

## Quy trình nén WebUI `.gz`

`platformio.ini` đang cấu hình:

```ini
[platformio]
data_dir = .pio/littlefs_data
default_envs = esp32

[env]
extra_scripts =
  pre:scripts/gzip_webui.py

[env:esp32]
board = esp32dev

[env:esp32-psram]
board = esp32dev
build_flags = ${env.build_flags} -DBOARD_HAS_PSRAM ...

[env:esp32-wrover]
board = freenove_esp32_wrover

[env:esp32s3]
board = esp32-s3-devkitc-1

[env:esp32s3-n8r2]
board = esp32-s3-devkitc-1
board_build.partitions = partitions_vbot_ota_8mb.csv
board_build.flash_size = 8MB
board_build.psram_type = qspi

[env:esp32s3-n8r8]
board = esp32-s3-devkitc-1
board_build.partitions = partitions_vbot_ota_8mb.csv
board_build.flash_size = 8MB
board_build.psram_type = opi

[env:esp32s3-n16r8]
board = esp32-s3-devkitc-1
board_build.partitions = partitions_vbot_ota_16mb.csv
board_build.flash_size = 16MB
board_build.psram_type = opi
```

Vì vậy mỗi lần chạy lệnh PlatformIO như `run`, `buildfs`, `upload`, hoặc `uploadfs`, script `scripts/gzip_webui.py` sẽ tự chạy trước.

Script này làm các việc sau:

- Xóa và tạo lại thư mục `.pio/littlefs_data/`.
- Đọc file gốc từ `data/`.
- Tự nén các file `.html`, `.css`, `.js` thành `.gz`.
- Copy các file khác, ví dụ `.mp3`, sang LittleFS staging.

Bạn không cần chạy thêm lệnh gzip thủ công.

## Build firmware

Chạy khi sửa code firmware, ví dụ `src/main.cpp`.

Chọn đúng môi trường build theo phần cứng:

```text
esp32             = ESP32 thường, dual-core, không PSRAM
esp32-psram       = ESP32 thường dual-core có PSRAM, generic esp32dev
esp32-wrover      = ESP32-WROVER / board ESP32 có PSRAM kiểu WROVER
esp32-psram-8mb   = ESP32 dual-core có PSRAM và flash 8MB
esp32-psram-16mb  = ESP32 dual-core có PSRAM và flash 16MB
esp32s3           = ESP32-S3 N8 không PSRAM
esp32s3-n8r2      = ESP32-S3 8MB flash + 2MB QSPI PSRAM
esp32s3-n8r8      = ESP32-S3 8MB flash + 8MB OPI PSRAM
esp32s3-n16r8     = ESP32-S3 16MB flash + 8MB OPI PSRAM
```

```powershell
cd C:\Users\PC-Tuyen\Desktop\VBot_ToiUu\esp32_vbot_client

#Lệnh Build toàn bộ device được hỗ trợ:
Lệnh build FW toàn bộ cho các device:
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32 -e esp32-psram -e esp32-wrover -e esp32-psram-8mb -e esp32-psram-16mb -e esp32s3 -e esp32s3-n8r2 -e esp32s3-n8r8 -e esp32s3-n16r8

Lệnh build FS toàn bộ cho các device:
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32 -e esp32-psram -e esp32-wrover -e esp32-psram-8mb -e esp32-psram-16mb -e esp32s3 -e esp32s3-n8r2 -e esp32s3-n8r8 -e esp32s3-n16r8 -t buildfs


# ESP32 thường
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32

# ESP32 thường có PSRAM
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32-psram

# ESP32 WROVER
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32-wrover

# ESP32 thường có PSRAM + flash 8MB / 16MB
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32-psram-8mb
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32-psram-16mb

# ESP32-S3 thường
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32s3

# ESP32-S3 N8R2
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32s3-n8r2

# ESP32-S3 N8R8
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32s3-n8r8

# ESP32-S3 N16R8
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32s3-n16r8
```

File firmware sau build nằm tại:

```text
.pio/build/esp32/firmware.bin
.pio/build/esp32-psram/firmware.bin
.pio/build/esp32-wrover/firmware.bin
.pio/build/esp32-psram-8mb/firmware.bin
.pio/build/esp32-psram-16mb/firmware.bin
.pio/build/esp32s3/firmware.bin
.pio/build/esp32s3-n8r2/firmware.bin
.pio/build/esp32s3-n8r8/firmware.bin
.pio/build/esp32s3-n16r8/firmware.bin
```

## Build filesystem LittleFS

Chạy khi sửa WebUI trong `data/*.html`, `data/*.css`, `data/*.js` hoặc file âm thanh trong `data/sound/`.

```powershell
cd C:\Users\PC-Tuyen\Desktop\VBot_ToiUu\esp32_vbot_client

# ESP32 thường
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32 -t buildfs

# ESP32 thường có PSRAM
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32-psram -t buildfs

# ESP32 WROVER
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32-wrover -t buildfs

# ESP32 thường có PSRAM + flash 8MB / 16MB
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32-psram-8mb -t buildfs
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32-psram-16mb -t buildfs

# ESP32-S3 thường
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32s3 -t buildfs

# ESP32-S3 N8R2
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32s3-n8r2 -t buildfs

# ESP32-S3 N8R8
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32s3-n8r8 -t buildfs

# ESP32-S3 N16R8
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32s3-n16r8 -t buildfs
```

File LittleFS sau build nằm tại:

```text
.pio/build/esp32/littlefs.bin
.pio/build/esp32-psram/littlefs.bin
.pio/build/esp32-wrover/littlefs.bin
.pio/build/esp32-psram-8mb/littlefs.bin
.pio/build/esp32-psram-16mb/littlefs.bin
.pio/build/esp32s3/littlefs.bin
.pio/build/esp32s3-n8r2/littlefs.bin
.pio/build/esp32s3-n8r8/littlefs.bin
.pio/build/esp32s3-n16r8/littlefs.bin
```

## Build và nạp trực tiếp firmware vào ESP32

Chạy khi sửa firmware.

```powershell
cd C:\Users\PC-Tuyen\Desktop\VBot_ToiUu\esp32_vbot_client

# ESP32 thường
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32 -t upload --upload-port COM11

# ESP32 thường có PSRAM
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32-psram -t upload --upload-port COM11

# ESP32 WROVER
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32-wrover -t upload --upload-port COM11

# ESP32 thường có PSRAM + flash 8MB / 16MB
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32-psram-8mb -t upload --upload-port COM11
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32-psram-16mb -t upload --upload-port COM11

# ESP32-S3 thường
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32s3 -t upload --upload-port COM11

# ESP32-S3 N8R2
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32s3-n8r2 -t upload --upload-port COM11

# ESP32-S3 N8R8
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32s3-n8r8 -t upload --upload-port COM11

# ESP32-S3 N16R8
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32s3-n16r8 -t upload --upload-port COM11
```

## Build và nạp trực tiếp WebUI/LittleFS vào ESP32

Chạy khi chỉ sửa WebUI hoặc âm thanh trong `data/`.

```powershell
cd C:\Users\PC-Tuyen\Desktop\VBot_ToiUu\esp32_vbot_client

# ESP32 thường
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32 -t uploadfs --upload-port COM11

# ESP32 thường có PSRAM
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32-psram -t uploadfs --upload-port COM11

# ESP32 WROVER
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32-wrover -t uploadfs --upload-port COM11

# ESP32 thường có PSRAM + flash 8MB / 16MB
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32-psram-8mb -t uploadfs --upload-port COM11
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32-psram-16mb -t uploadfs --upload-port COM11

# ESP32-S3 thường
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32s3 -t uploadfs --upload-port COM11

# ESP32-S3 N8R2
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32s3-n8r2 -t uploadfs --upload-port COM11

# ESP32-S3 N8R8
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32s3-n8r8 -t uploadfs --upload-port COM11

# ESP32-S3 N16R8
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32s3-n16r8 -t uploadfs --upload-port COM11
```

Lệnh `uploadfs` sẽ tự chạy `scripts/gzip_webui.py`, build LittleFS và nạp filesystem vào ESP32. Không cần chạy `buildfs` riêng trước đó nếu mục tiêu là nạp luôn.

## Quy trình thường dùng

Trong các ví dụ bên dưới đang dùng `-e esp32`. Nếu nạp cho board khác thì đổi sang env tương ứng, ví dụ `-e esp32-psram`, `-e esp32-wrover`, `-e esp32s3-n8r2`, `-e esp32s3-n8r8` hoặc `-e esp32s3-n16r8`.

Sửa WebUI:

```text
1. Sửa file trong data/
2. Chạy uploadfs
3. Mở lại WebUI trên trình duyệt
```

Lệnh:

```powershell
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32 -t uploadfs --upload-port COM11
```

Sửa firmware:

```text
1. Sửa src/main.cpp
2. Chạy upload firmware
3. ESP32 tự reset sau khi nạp
```

Lệnh:

```powershell
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32 -t upload --upload-port COM11
```

Sửa cả firmware và WebUI:

```powershell
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32 -t upload --upload-port COM11
& "C:\Users\PC-Tuyen\.platformio\penv\Scripts\platformio.exe" run -e esp32 -t uploadfs --upload-port COM11
```

## Sau khi flash

Nếu chưa có WiFi, ESP32 mở AP `VBot-ESP32-Setup`. Kết nối vào AP này để cấu hình WiFi.

Sau khi ESP32 đã vào WiFi, WebUI nằm tại IP của ESP32. OTA firmware nằm tại:

```text
http://<IP_ESP32>/update
```

## Lưu ý phần cứng

- Firmware này yêu cầu chip ESP có ít nhất 2 CPU core. Code dùng nhiều FreeRTOS task và có task pin trực tiếp vào core 1, vì vậy không hỗ trợ chip 1 core.
- Thiết bị phù hợp: ESP32 thường dual-core, ESP32-WROOM dual-core, ESP32-WROVER/ESP32 dual-core có PSRAM, ESP32-S3 N8, ESP32-S3 N8R2, ESP32-S3 N8R8, ESP32-S3 N16R8.
- Không dùng cho các dòng 1 core như ESP32-S2, ESP32-C2, ESP32-C3, ESP32-C6, ESP32-H2 hoặc các board Arduino-ESP32 đang build ở chế độ unicore.
- Nếu build nhầm target unicore, firmware sẽ báo lỗi lúc compile. Nếu chạy trên phần cứng chỉ có 1 core, chương trình sẽ dừng ngay đầu `setup()` và in lỗi ra Serial.
- INMP441 dùng I2S RX riêng.
- MAX98357 dùng I2S TX. Code sẽ tạm dừng MP3 decoder khi phát PCM raw realtime và khởi tạo lại I2S TX theo cấu hình hiện tại.
- LED dùng FastLED. GPIO WS2812 được lưu trong NVS và áp dụng sau khi ESP restart.
- Server là WebSocket, ví dụ `ws://192.168.1.10:5003`, không phải TCP socket thô.
- `session_id` gửi tới WebSocket được tạo ngẫu nhiên ở mỗi phiên kết nối, dùng giá trị Session ID trong WebUI làm prefix.
- Phiên bản firmware hiện tại nằm trong biến `VBOT_CLIENT_VERSION` và hiển thị trong `/VBot_Client_Info`.
- Default GPIO đã tránh các chân boot-strapping nhạy cảm của ESP32. Mặc định mới: LED 23, nút Mic 32, Volume+ 33, Volume- 18, WakeUP 19, INMP441 BCLK 14/WS 13/DOUT 34, MAX98357 BCLK 27/LRC 26/DIN 25.
