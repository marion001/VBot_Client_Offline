# VBot Phicomm R1 Android Client

Android client cho loa Phicomm R1, giao tiếp với VBot server qua WebSocket và có WebUI quản trị nội bộ.

## 1. Tính năng chính

- Tự chạy service nền sau khi boot.
- WebUI local tại `http://<ip-loa>:8081`.
- Kết nối WebSocket tới VBot server theo cấu hình `host/port/path`
- Thu mic PCM và gửi realtime lên server.
- Hỗ trợ wakeword:
  - `server` (server xử lý wakeword)
  - `client` (Snowboy xử lý tại thiết bị)
  - `none` (không wakeword, chỉ nút bấm)
- Hỗ trợ nhiều file hotword Snowboy (`.pmdl`, `.umdl`):
  - bật/tắt từng model
  - sensitivity theo từng model
  - upload/xóa model custom từ WebUI
- Điều khiển nhanh từ WebUI:
  - start/stop recording
  - stop audio
  - LED test
  - Bluetooth toggle
  - Wi-Fi scan/connect
  - gửi socket command
  - chạy shell command
- Kiểm tra phiên bản mới từ GitHub API và hiển thị hướng dẫn cập nhật bằng ADB.

## 2. Kiến trúc tổng quan

- `VBotClientService`: lõi runtime (audio, websocket, command, trạng thái).
- `MiniWebUiServer`: HTTP server cục bộ, render WebUI và xử lý API.
- `AudioEngine`: capture mic + playback.
- `SnowboyClientWakeDetector`: detect wakeword phía client.
- `Config`: lưu/đọc cấu hình bằng `SharedPreferences`.
- `Version`: chứa version app + URL kiểm tra update.

## 3. Cơ chế kết nối server

### 3.1 WebSocket

- URL socket dựng từ:
  - `host`
  - `port`
  - `path`
- Ví dụ: `ws://192.168.1.10:5003/`

### 3.2 Audio stream

- Sample rate: `16000 Hz`
- PCM: `signed 16-bit little-endian`, mono
- Wake frame: `1024 bytes`

### 3.3 Wakeword mode

- `server`: gửi mic cho server để server xử lý wakeword.
- `client`: Snowboy detect local, trúng wakeword sẽ gửi `Skip_WakeUP`/start flow recording.
- `none`: không detect wakeword, chỉ thao tác thủ công bằng api hoặc nút nhấn

## 4. WebUI

Mở tại:

`http://<ip-loa>:8081`

### 4.1 Nhóm chức năng

- `Configuration`:
  - Client/Server
  - Audio core
  - Snowboy Models
  - DSP/VAD
  - Behavior/Logs
- `Socket Command`
- `Shell Command`
- `Wi-Fi Tools`
- `Bluetooth`
- `Play Audio`
- `Restore Config`
- `Kiem Tra Cap Nhat`

### 4.2 Snowboy Models

- Bảng gồm: thứ tự, model, active, sensitivity, xóa.
- Model built-in trong `assets/snowboy` không cho xóa.
- Model upload custom lưu trong thư mục app và có thể xóa.
- Nếu không active model nào thì Snowboy không detect (no wakeword).

## 5. Kiểm tra cập nhật

App gọi GitHub API:
WebUI sẽ hiển thị hướng dẫn cập nhật ADB từng bước.

## 6. Cài đặt qua ADB

```powershell
adb connect <ip>:5555
adb -s <ip>:5555 push app\build\outputs\apk\debug\app-debug.apk /data/local/tmp/vbot.apk
adb -s <ip>:5555 shell pm install -r /data/local/tmp/vbot.apk
adb -s <ip>:5555 shell rm /data/local/tmp/vbot.apk
adb -s <ip>:5555 shell am start -n com.vbot.phicommr1/.MainActivity

Cấp quyền button nút nhấn
adb -s <ip>:5555 shell pm grant com.vbot_client.phicommr1 android.permission.WRITE_SECURE_SETTINGS
```

## 7. Ghi chú vận hành

- Silent install từ trong app Android thường bị giới hạn quyền hệ thống.
- Cách ổn định nhất hiện tại: cập nhật bằng ADB từ PC cùng mạng LAN.
- Sau mỗi thay đổi logic WebUI/runtime, nên build lại và kiểm tra:
  - mở WebUI
  - save config + reconnect
  - wakeword detect/log
  - websocket trạng thái kết nối
 
<img width="1902" height="1092" alt="Image" src="https://github.com/user-attachments/assets/363614f6-bc84-40e4-9ab7-e616e1a3532c" />
