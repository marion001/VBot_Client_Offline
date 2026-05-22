Yêu Cầu: Thiết Bị Raspberry Pi Chạy VBot Làm Server, Hoặc Sử Dung Loa Thông Minh Chạy VBot
- 
- Bạn muốn code Client cho loa VBot trên các nền tảng, thiết bị khác qua kết nối WebSocket có thể tham khảo 2 File sau:
	 + README_Socket.md
	 + Test_Client_WebSocket_Streaming.html
  
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

# Hướng Dẫn Nạp Firmware ESP32 VBot Client - Chế Độ Socket

Tài liệu này hướng dẫn nạp firmware cho ESP32 VBot Client chạy ở chế độ Socket/WebSocket.

## 1. File cần chuẩn bị
	Cần có các file `.bin` sau trong thư mục bin:
		bootloader.bin
		partitions.bin
		firmware.bin
		littlefs.bin
		
	Phần mềm: ESP Flash Download Tool

## 2. Thông tin phân vùng flash (Mẫu với ESP32)
	Ví Dụ Mẫu Firmware này sử dụng cho esp32 partition như sau:
	
	# Name,   Type, SubType, Offset,   Size,     Flags
	nvs,      data, nvs,     0x9000,   0x5000,
	otadata,  data, ota,     0xe000,   0x2000,
	app0,     app,  ota_0,   0x10000,  0x1A0000,
	app1,     app,  ota_1,   0x1B0000, 0x1A0000,
	littlefs, data, spiffs,  0x350000, 0xA0000,
	coredump, data, coredump,0x3F0000, 0x10000,
- LƯU Ý:
  + Để xem đúng phân vùng partition tương ứng với thiết bị của bạn hãy xem file: flash_download_tool_entries.txt
  + Phần này chỉ để tham khảo cơ cấu chia phân vùng của Firmware

## 3. Bắt đầu tiến hành nạp firmware (Mẫu với ESP32)
	Mở phần mềm ESP Flash Download Tool và chọn:
	
		Chip Type: ESP32
		WorkMode: Develop
		LoadMode: UART

	Cấu hình khuyến nghị (Mẫu ESP32):
	
		SPI SPEED : 40MHz
		SPI MODE  : DIO
		FLASH SIZE: 4MB
		BAUD      : 921600

	Khi nạp bằng ESP Flash Download Tool hoặc esptool, cần điền đúng các giá trị và lần lượt thứ tự file, Offset (Mẫu ESP32):
	Tick chọn từng file và nhập offset tương ứng:
		| File             | Offset     |
		| ---------------- | ---------- |
		| `bootloader.bin` | `0x1000`   |
		| `partitions.bin` | `0x8000`   |
		| `firmware.bin`   | `0x10000`  |
		| `littlefs.bin`   | `0x350000` |

	Sau đó:
	
		1. Chọn đúng cổng COM.
		2. Bấm START.
		3. Nếu ESP32 không tự vào chế độ nạp:
			- Giữ nút BOOT.
			- Nhấn nút EN/RST.
			- Thả EN/RST.
			- Thả BOOT.

	- LƯU Ý: Để nhập đúng phân vùng Offset tương ứng với thiết bị của bạn hãy xem file flash_download_tool_entries.txt để nhập cho đúng
  
## 4. Nạp bằng esptool dòng lệnh (Mẫu với ESP32)
	Có thể nạp đầy đủ bằng lệnh:
		python -m esptool --chip esp32 --port COM11 --baud 921600 write_flash -z ^
		0x1000 bootloader.bin ^
		0x8000 partitions.bin ^
		0x10000 firmware.bin ^
		0x350000 littlefs.bin
		
	- LƯU Ý: Để nhập đúng phân vùng Offset tương ứng với thiết bị của bạn hãy xem file flash_download_tool_entries.txt để nhập cho đúng
  
## 5. Erase flash khi cần (Mẫu với ESP32)
	Nếu đổi partition hoặc nạp bản mới hoàn toàn, nên xóa flash trước:
		python -m esptool --chip esp32 --port COM11 erase_flash

	Sau đó nạp lại đầy đủ các file .bin.
	
	- LƯU Ý: Để nhập đúng phân vùng Offset tương ứng với thiết bị của bạn hãy xem file flash_download_tool_entries.txt để nhập cho đúng
## 6. Sau khi nạp xong (mẫu với ESP32)

	ESP32 sẽ khởi động lại và chạy VBot Client.
	Nếu chưa có WiFi, ESP32 sẽ phát WiFi cấu hình.

	Kết nối vào WiFi cấu hình và thiết lập:
		- Tên WiFi
		- Mật khẩu WiFi
		- IP máy chủ VBot Socket
		- Port Socket

	Ví dụ cấu hình Socket:
		- Host: 192.168.14.175
		- Port: 5003
		
	Khi kết nối thành công, có thể kết nối xem logs Serial sẽ hiện tương tự:
		- Đã kết nối WebSocket
		- Gửi cấu hình tới máy chủ VBot
		- Đã nhận cấu hình Client

		[108] Booting - Khởi động chương trình VBot Client ESP32
		*wm:AutoConnect 
		*wm:Connecting to SAVED AP: PhongNgu
		*wm:connectTimeout not set, ESP waitForConnectResult... 
		*wm:AutoConnect: SUCCESS 
		*wm:STA IP Address: 192.168.14.106
		[850] Địa chỉ kết nối WebSocket ws://192.168.14.175:5003/
		[852] Audio URL: http://192.168.14.175/assets/sound/welcome/computer-startup.mp3
		[3043] Đã kết nối WebSocket
		[3046] Gửi cấu hình tới máy chủ VBot: {"session_id":"VBot_ESP32_Client_Tuyen_4aca4b70_75cdd7c7","client_name":"VBot ESP32 Client Tuyen","working_mode":"main_processing","conversation_mode":true}
		[Máy Chủ VBot]: {"vbot_client_id": "('192.168.14.106', 52368)", "message": "Đã kết nối tới Server VBot Socket"}
		[Máy Chủ VBot]: {"vbot_client_id": "VBot_ESP32_Client_Tuyen_4aca4b70_75cdd7c7", "working_mode": "main_processing", "message": "Đã nhận cấu hình Client"}
		

## 7. Cập nhật OTA qua WebUI của thiết bị
	Firmware có thể hỗ trợ cập nhật qua Web OTA.
	
	Trong giao diện WebUi nhấn vào: Flash Chương Trình, Firmware
	
		- Upload firmware.bin để cập nhật chương trình. trong giao diện Flash Mục: OTA Mode -> Firmware
		- Upload littlefs.bin để cập nhật WebUI/filesystem trong giao diện Flash Mục: OTA Mode -> LittleFS / SPIFFS
	
	Không nên cập nhật partitions.bin qua Web OTA.
	Nếu đổi partition, nên nạp lại bằng USB/esptool hoặc Flash Download Tool.

## 8. Lưu ý quan trọng
	- Không nạp sai offset.
	- littlefs.bin phải nạp tại 0x350000.
	- firmware.bin phải nạp tại 0x10000.
	- Nếu WebUI trắng hoặc thiếu file, kiểm tra lại LittleFS.
	- Nếu đổi partition, bắt buộc erase flash rồi nạp lại đầy đủ.
	- Nên dùng SPI SPEED 40MHz và SPI MODE DIO để ổn định.
  	- Khi Flash Xong Dùng Nguồn CỔng Từ USB Sẽ Bị Thiếu Nguồn Khiến ESP Bị RESET Liên Tục
  	- Nên Dùng Nguồn 5V-2A trở lên Để LED Được Sáng Ổn Định

## Sau khi flash

Nếu chưa có WiFi, ESP32 mở AP `VBot-ESP32-Setup`. Kết nối vào AP này để cấu hình WiFi.

Sau khi ESP32 đã vào WiFi, WebUI nằm tại IP của ESP32. OTA firmware nằm tại:

```text
http://<IP_ESP32>/update
```

## Lưu ý phần cứng

- INMP441 dùng I2S RX riêng.
- MAX98357 dùng I2S TX. Code sẽ tạm dừng MP3 decoder khi phát PCM raw realtime và khởi tạo lại I2S TX theo cấu hình hiện tại.
- LED dùng FastLED. GPIO WS2812 được lưu trong NVS và áp dụng sau khi ESP restart.
- Server là WebSocket, ví dụ `ws://192.168.1.10:5003`, không phải TCP socket thô.
- `session_id` gửi tới WebSocket được tạo ngẫu nhiên ở mỗi phiên kết nối, dùng giá trị Session ID trong WebUI làm prefix.
- Phiên bản firmware hiện tại nằm trong biến `VBOT_CLIENT_VERSION` và hiển thị trong `/VBot_Client_Info`.
- Default GPIO đã tránh các chân boot-strapping nhạy cảm của ESP32. Mặc định mới: LED 23, nút Mic 32, Volume+ 33, Volume- 18, WakeUP 19, INMP441 BCLK 14/WS 13/DOUT 34, MAX98357 BCLK 27/LRC 26/DIN 25.

<img width="990" height="802" alt="Image" src="https://github.com/user-attachments/assets/1aa26732-6459-4e19-a5cb-9c441afe3a66" />
<img width="1452" height="987" alt="Image" src="https://github.com/user-attachments/assets/cb2bbb2c-d1ad-45b1-a0d1-34d0ca77fcdb" />