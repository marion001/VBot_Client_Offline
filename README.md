# ESP32 VBot Socket Client

WebUI có thể chọn khởi chạy kết nối máy chủ VBot hoặc chạy độc lập. Chế độ độc lập không khởi tạo Mic/WebSocket; MQTT, mDNS, phát âm thanh, nút nhấn, LED, WebUI và OTA vẫn hoạt động.

Client ESP32 cho `Streaming.py` khi server đặt `connection_protocol = "socket"`.

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
