# ESP32 VBot Socket Client (Làm Client Cho Loa Chủ Đang Chạy VBot Assistant)

## Có thể sử dụng ESP32 hoặc Loa PhicommR1 làm CLient Tương Ứng Theo Từng Thư Mục

Lưu ý quan trọng: bootloader ESP32 dùng offset `0x1000`, còn bootloader ESP32-S3 dùng offset `0x0000`. Không dùng chung offset giữa hai dòng chip.

WebUI có thể chọn khởi chạy kết nối máy chủ VBot hoặc chạy độc lập. Chế độ độc lập không khởi tạo Mic/WebSocket; MQTT, mDNS, phát âm thanh, nút nhấn, LED, WebUI và OTA vẫn hoạt động.

Client ESP32 cho `Streaming.py` khi server đặt `connection_protocol = "socket"`.

- [Hướng dẫn flash fimware Client](README_FLASH.md)
- [Trang Chính VBot Assistant (Loa Chủ)](https://github.com/marion001/VBot_Offline)
## Chọn và nhập liệu đúng theo bảng sau khi nạp bằng Espressif Flash Download Tool

| Environment | Chip | Flash | PSRAM | bootloader.bin | partitions.bin | firmware.bin | littlefs.bin |
|---|---|---:|---:|---:|---:|---:|---:|
| `esp32` | ESP32 | 4 MB | Không | `0x1000` | `0x8000` | `0x10000` | `0x350000` |
| `esp32-psram` | ESP32 | 4 MB | Có | `0x1000` | `0x8000` | `0x10000` | `0x350000` |
| `esp32-wrover` | ESP32-WROVER | 4 MB | Có | `0x1000` | `0x8000` | `0x10000` | `0x350000` |
| `esp32-psram-8mb` | ESP32 | 8 MB | Có | `0x1000` | `0x8000` | `0x10000` | `0x610000` |
| `esp32-psram-16mb` | ESP32 | 16 MB | Có | `0x1000` | `0x8000` | `0x10000` | `0x810000` |
| `esp32-wakenet` | ESP32 | 16 MB | Có | `0x1000` | `0x8000` | `0x10000` | `0x810000` *(srmodels: `0xC00000`, boot_app0: `0xE000`)* |
| `esp32s3` | ESP32-S3 | 8 MB | Không | `0x0000` | `0x8000` | `0x10000` | `0x610000` |
| `esp32s3-n8r2` | ESP32-S3 | 8 MB | 2 MB QSPI | `0x0000` | `0x8000` | `0x10000` | `0x610000` |
| `esp32s3-n8r2-wakenet` | ESP32-S3 | 8 MB | 2 MB QSPI | `0x0000` | `0x8000` | `0x10000` | `0x490000` *(srmodels: `0x670000`, boot_app0: `0xE000`)* |
| `esp32s3-n8r8` | ESP32-S3 | 8 MB | 8 MB OPI | `0x0000` | `0x8000` | `0x10000` | `0x610000` |
| `esp32s3-n8r8-wakenet` | ESP32-S3 | 8 MB | 8 MB OPI | `0x0000` | `0x8000` | `0x10000` | `0x490000` *(srmodels: `0x670000`, boot_app0: `0xE000`)* |
| `esp32s3-n16r8` | ESP32-S3 | 16 MB | 8 MB OPI | `0x0000` | `0x8000` | `0x10000` | `0x810000` |
| `esp32s3-n16r8-wakenet` | ESP32-S3 | 16 MB | 8 MB OPI | `0x0000` | `0x8000` | `0x10000` | `0x810000` *(srmodels: `0xC00000`, boot_app0: `0xE000`)* |
| `Bluetooth_ESP32_RECEIVER` | ESP32 | 4 MB | Không | `0x1000` | `0x8000` | `0x10000` | Không dùng *(boot_app0: `0xE000`)* |

## Module Thu Âm Thanh Bluetooth A2DP (ESP32 Thường)

Dành riêng cho việc nâng cấp loa VBot Client thành một chiếc loa Bluetooth thông minh cao cấp. Firmware và hướng dẫn nạp được đóng gói độc lập tại thư mục [Bluetooth_ESP32_RECEIVER](Bluetooth_ESP32_RECEIVER/):

* **Âm thanh số thuần túy qua I2S**: Nhận luồng Bluetooth Stereo 44.1kHz và truyền trực tiếp sang ESP32-S3 qua **I2S Slave RX** mà không qua DAC trung gian, chống nhiễu tuyệt đối.
* **Giao tiếp hai chiều qua UART (115200 baud)**:
  * Tự động nhận Tên Bluetooth từ cấu hình WebUI của ESP32-S3 (`cname`).
  * Tự động bật/tắt Bluetooth theo cài đặt trên WebUI / NVS của ESP32-S3.
  * Tự động nhận diện và đọc tên thiết bị kết nối (**Peer Device Name** như iPhone, Android, PC...) qua Bluetooth GAP.
  * Đồng bộ Metadata bài hát (**Title, Artist, Album**) sang ESP32-S3 để hiển thị lên WebUI và Home Assistant.
  * Nhận lệnh điều khiển `PLAY`, `PAUSE`, `NEXT`, `PREV`, `DISCONNECT` từ phím bấm cứng, WebUI VBot hoặc các Automation/Dashboard của Home Assistant.
* **Cơ chế chuyển tiếp Zero-Collision**:
  * Khi người dùng tạm dừng nhạc (Pause/Stop) trên điện thoại hoặc ngắt kết nối, ESP32-S3 tự động ngắt I2S Slave RX, trả lại cổng I2S_NUM_1 cho Microphone INMP441 và WakeNet ("VBot ơi") hoạt động bình thường. Luồng phát âm thanh luôn được giữ liên tục qua các nốt lặng và đoạn chuyển bài mà không bị ngắt tiếng.
  * Hỗ trợ cấu hình động toàn bộ chân GPIO của ESP32 Thường trực tiếp qua Tab WebUI riêng **Cấu Hình GPIO Bluetooth ESP32**.
  * Khi nhạc phát lại, tự động chuyển DAC Loa sang 44.1kHz Stereo mượt mà.

### Sơ đồ nối dây giữa ESP32 Thường và ESP32-S3

| Tín hiệu | ESP32 Thường (A2DP Receiver) | ESP32-S3 (VBot Client) | Ghi chú |
| :--- | :--- | :--- | :--- |
| **I2S BCLK** | **GPIO 26** | **GPIO 14** | Xung bit clock I2S Master -> Slave |
| **I2S WS (LRCK)** | **GPIO 25** | **GPIO 21** | Xung chọn kênh L/R I2S Master -> Slave |
| **I2S DATA OUT** | **GPIO 22** | **GPIO 47 (DIN)** | Dữ liệu âm thanh PCM từ ESP32 sang S3 |
| **UART TX** | **GPIO 17 (TX2)** | **GPIO 2 (RX)** | ESP32 gửi metadata & trạng thái sang S3 |
| **UART RX** | **GPIO 16 (RX2)** | **GPIO 1 (TX)** | ESP32 nhận cấu hình & điều khiển từ S3 |
| **GND** | **GND** | **GND** | **Bắt buộc nối chung GND** |
| **Nguồn cấp** | **5V (VIN)** | **5V (VIN)** | Khuyên dùng nguồn 5V chung 2A-3A |

### 4 Nút Bấm Cứng Vật Lý Trên ESP32 Thường (Nối GPIO với GND):
- **Nút 1 (GPIO 32)**: Nhấn nhả $\rightarrow$ **Play / Pause**; Nhấn giữ $\rightarrow$ **Ngắt kết nối** Bluetooth (Disconnect).
- **Nút 2 (GPIO 33)**: Nhấn nhả $\rightarrow$ **Next** bài; Nhấn đúp $\rightarrow$ **Phát lại từ đầu** bài hiện tại; Nhấn giữ $\rightarrow$ **Tăng âm lượng**.
- **Nút 3 (GPIO 27)**: Nhấn nhả $\rightarrow$ **Previous** bài; Nhấn đúp $\rightarrow$ **Phát lại từ đầu** bài hiện tại; Nhấn giữ $\rightarrow$ **Giảm âm lượng**.
- **Nút 4 (GPIO 13)**: Nhấn nhả $\rightarrow$ **Đồng bộ trạng thái** sang ESP32-S3; Nhấn giữ > 2 giây $\rightarrow$ **Khởi động lại ESP32**.

## Tính năng

- **Hỗ trợ kết nối âm thanh không dây Apple AirPlay 1 (AirTunes Receiver)**:
  - Tự động quảng bá dịch vụ Bonjour mDNS (`_raop._tcp`, `_airplay._tcp`), hiển thị tức thì trên **Trung tâm điều khiển (Control Center)** của iPhone, iPad, Mac và các ứng dụng phát nhạc Apple Music, Spotify, YouTube Music.
  - Nhận luồng âm thanh kỹ thuật số không nén (ALAC/PCM 44.1kHz Stereo 16-bit), mang lại chất lượng âm thanh phòng thu vượt trội.
  - Hỗ trợ đồng bộ âm lượng 2 chiều giữa thiết bị Apple và loa VBot, hiển thị LED nhịp điệu theo bài hát.
  - Tự động nhường quyền thông minh: Tạm dừng hoặc giảm âm lượng AirPlay khi người dùng gọi "VBot ơi" và khôi phục mượt mà khi xử lý xong.
- **Hỗ trợ kết nối âm thanh không dây DLNA / UPnP MediaRenderer (DMR)**:
  - Tự động nhận diện trên mạng Wi-Fi LAN qua giao thức SSDP Discovery.
  - Tương thích rộng rãi với các thiết bị Android, Windows PC ("Cast to Device" / Windows Media Player), Smart TV, NAS và Home Assistant (BubbleUPnP, MusicBee, Foobar2000, v.v.).
  - Phát trực tiếp các định dạng âm thanh chất lượng cao: MP3, AAC, FLAC, WAV từ mạng cục bộ hoặc máy chủ nội bộ.
  - Hỗ trợ đầy đủ lệnh điều khiển: Play, Pause, Stop, Seek (tua bài), tăng/giảm âm lượng.
- **Tùy chọn bật/tắt độc lập AirPlay & DLNA**: Người dùng có thể dễ dàng kích hoạt hoặc tạm tắt từng tính năng trực tiếp trên WebUI hoặc lưu vào NVS.
- **Tab WebUI riêng `Bluetooth Control`**: Điều khiển phát/tạm dừng, bài tiếp theo/bài trước, ngắt kết nối, thanh trượt âm lượng, hiển thị tên loa Bluetooth, tên điện thoại đang kết nối và thông tin bài hát thời gian thực.
- **Tab WebUI `Cấu Hình GPIO (LED, MIC, Bluetooth, Audio)`**: Cấu hình trực quan toàn bộ chân phần cứng (LED WS2812, Mic INMP441, DAC Loa MAX98357, nút bấm, rotary encoder, chân Bluetooth I2S & UART).
- **Đồng bộ toàn diện MQTT Home Assistant**: Xuất bản tự động các cảm biến Bluetooth (`sensor/bluetooth_connection`, `sensor/bluetooth_device_name`, `sensor/bluetooth_playing`, `sensor/bluetooth_title`, `sensor/bluetooth_artist`, `sensor/bluetooth_album`, `sensor/bluetooth_enabled`) và entity `media_player` chuẩn Home Assistant.
- **Nhận diện từ khóa đánh thức WakeNet9 On-Device (`esp32s3-n16r8-wakenet`)**: Xử lý nhận diện giọng nói đánh thức hoàn toàn Offline trên chip (~0ms), không tốn băng thông Wi-Fi và không phụ thuộc máy chủ khi ở trạng thái chờ.
- **Tab WebUI riêng `Cấu Hình Hotword WakeUP` (dành riêng cho bản WakeNet)**: Tích hợp sẵn 4 từ khóa chuẩn (*Hi Lily, Alexa, Sophia, Jarvis*) và cho phép tải lên tối đa **5 từ khóa tùy chỉnh** (`.bin`) trực tiếp qua WebUI.
- **Kho 18 mẫu Hotword WakeNet9 có sẵn**: Tải về miễn phí tại thư mục [ESP_WakeNet_Hotword_Sample](ESP_WakeNet_Hotword_Sample).
- **Cơ chế bảo vệ Hotword an toàn**: 4 Hotword mặc định được lưu trên phân vùng Flash và khóa cố định; người dùng chỉ có thể xóa các Hotword do chính mình tải lên.
- **Tùy chọn bản chuẩn (`esp32s3-n16r8`)**: Dành cho người dùng không cần WakeNet On-Device, tối ưu dung lượng phân vùng LittleFS lên trọn vẹn 8MB cho WebUI và các file tài nguyên âm thanh.

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
