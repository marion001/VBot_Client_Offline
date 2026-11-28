# VBot Phicomm R1 Android Client

- **Phiên bản phát hành hiện tại:** `28/11/2026-1` (Tích hợp Xiaozhi `2.2.6`)
- **Tên gói cài đặt:** `PhicommR1_VBotClient.apk` (`com.vbot_client.phicommr1`)
- **Thiết bị mục tiêu:** Loa thông minh Phicomm R1 (Android 5.1.1 / RK3229)

Ứng dụng Android Client dành riêng cho loa thông minh **Phicomm R1**, hỗ trợ hoạt động song song 2 chế độ (**VBot Server qua WebSocket** hoặc **Xiaozhi AI**), tích hợp nhận đồng bộ âm thanh đa phòng **Multiroom Audio Receiver**, kết nối **Home Assistant qua MQTT**, xử lý đánh thức **Snowboy Offline** và quản trị toàn diện qua giao diện **WebUI nội bộ** tại cổng `8081`.

---

## 1. Các tính năng nổi bật (Phiên bản `28/11/2026-1`)

- **Chế độ hoạt động kép (Dual Working Mode)**:
  - **VBot Assistant Client**: Giao tiếp WebSocket thời gian thực với Loa chủ / VBot Server (`ws://<host>:<port><path>`), đồng bộ trạng thái, thu mic PCM 16kHz và phát phản hồi trợ lý.
  - **Xiaozhi AI Client (`v2.2.6`)**: Kết nối trực tiếp máy chủ Xiaozhi với mã hóa âm thanh Opus và Jitter Buffer mượt mà.
- **Nhận âm thanh đa phòng — Multiroom Audio Receiver**:
  - Hoạt động làm loa nhánh nhận luồng âm thanh đồng bộ **PCM 48kHz** thời gian thực từ **Loa chủ VBot (Coordinator)** trong mạng LAN (UDP Control/Sync/Audio `50600`–`50602`).
  - Tự động quảng bá định danh mDNS (`vbot-<device_id>.local` và dịch vụ `_vbot._tcp`) để Loa chủ VBot và Home Assistant tự động khám phá.
  - Hỗ trợ tinh chỉnh bù độ trễ âm thanh riêng cho loa (`-150 ms` đến `+150 ms`), tạm dừng/tiếp tục/rời nhóm ngay trên WebUI hoặc từ Home Assistant.
- **Nhận diện từ khóa đánh thức (Wakeword)**:
  - Hỗ trợ 3 chế độ: `client` (Snowboy xử lý trực tiếp trên loa Offline), `server` (gửi luồng mic về VBot Server xử lý), `none` (chỉ kích hoạt bằng nút cứng hoặc API).
  - Quản lý đa mô hình Snowboy (`.pmdl`, `.umdl`): bật/tắt từng model, chỉnh độ nhạy riêng, tải lên/tải xuống/xóa model tùy chỉnh từ WebUI.
- **Bộ lọc thu âm Software MicDSP & VAD**:
  - Tự động kích hoạt chuỗi xử lý tín hiệu số phần mềm (**HPF 85Hz, khử vọng AEC, lọc nhiễu nền NS, cân bằng âm lượng AGC**) kết hợp **VAD** chống gửi khoảng lặng và công cụ kiểm tra Mic trực tiếp trên WebUI.
- **Giải trí, Âm thanh & Equalizer 6 dải tần**:
  - Phát nhạc trực tiếp từ **URL âm thanh, YouTube (tìm kiếm & quản lý Playlist), Zing MP3**.
  - **Đài Radio Online (VOV1, VOV2, VOV3, VOV Giao Thông HN/HCM...)** tích hợp bộ chuyển tiếp luồng nội bộ (`local_stream_proxy`) tương thích hoàn toàn với Android 5.1 và cho phép thêm/sửa kênh tùy ý.
  - Tự động lưu và khôi phục vị trí nghe dở cho **Truyện / Podcast**.
  - **Bộ chỉnh âm Equalizer 6 dải tần số** kèm Preset, Bass Boost, Loudness và Preamp (áp dụng đồng bộ cho cả Media và Multiroom).
  - Hỗ trợ **DLNA / UPnP MediaRenderer** và **Bluetooth A2DP**.
- **Tiện ích Gia đình & Lịch Âm - Dương Việt Nam Offline**:
  - Tính toán **Lịch Âm - Dương, Can Chi, Tiết Khí và Giờ Hoàng Đạo** hoàn toàn Offline trên thiết bị.
  - Tự động đọc lịch mỗi sáng, nhắc trước 1 ngày và nhắc đúng sáng **Mùng 1 / Ngày Rằm**; hỗ trợ chia đoạn đọc TTS tiếng Việt dài mượt mà không giới hạn ký tự.
  - **Báo thức thông minh đa tác vụ** (Chuông, Đọc TTS, Mở Đài Radio, Phát Playlist, Nhạc chuông MP3), **Điểm chuông báo giờ chẵn**, **Hẹn giờ tắt nhạc giảm dần âm lượng (Sleep Timer)** và khung giờ **Không làm phiền (DND)**.
  - Quản lý thư viện **Nhạc chuông MP3 tùy chỉnh**, phát **Cảnh báo thông minh** (`doorbell`, `security`, `fire`, `warning`, `chime`, `alarm`) và **Đàm thoại nội bộ (Intercom)** từ trình duyệt.
- **Tùy biến Nút nhấn cứng đỉnh loa & Hiệu ứng LED**:
  - Gán hành động độc lập cho **Nhấn 1 lần, Nhấn đúp, Nhấn 3 lần, Nhấn giữ** (Gọi trợ lý, Dừng/Tạm dừng nhạc & Multiroom, Đọc giờ, Đọc Lịch Âm, Bật/Tắt Mic, Mở Radio...).
- **Đồng bộ toàn diện với Home Assistant (MQTT Auto-Discovery)**:
  - Tự động tạo đầy đủ thực thể điều khiển Media, Âm lượng, Mic, LED, Wakeword, Equalizer, Đài Radio, Báo thức, Lịch Âm, Multiroom và thông số phần cứng (CPU, RAM, Nhiệt độ, Wi-Fi).
- **Cập nhật ứng dụng 1 chạm & Sao lưu cấu hình**:
  - Kiểm tra phiên bản mới từ GitHub (`Version.json`) và hỗ trợ **Tự động tải & cài đặt cập nhật APK trực tiếp trên WebUI** (hoặc cập nhật qua ADB), kèm sao lưu / khôi phục toàn bộ cấu hình JSON.

---

## 2. Giao diện WebUI & Khôi phục mật khẩu WebUI

Truy cập trên trình duyệt cùng mạng LAN tại địa chỉ:

```text
http://<IP_LOA>:8081
```

### Xem lại mật khẩu đăng nhập WebUI khi quên

- **Cách nhanh nhất:** Chạy `Update_R1.bat` trong thư mục `adb_tools_VBot_Client_PhicommR1_Auto_Install` và chọn mục **`6. Xem Mat Khau dang nhap WebUI VBot Client da luu`**.
- **Hoặc chạy lệnh ADB thủ công** (thay `<ip>` bằng địa chỉ IP của loa):

```powershell
adb connect <ip>:5555
adb -s <ip>:5555 shell run-as com.vbot_client.phicommr1 cat shared_prefs/vbot_r1.xml | findstr "webUiAuthEnabled webUiUsername webUiPassword"
```

Hoặc xem toàn bộ file cấu hình:

```powershell
adb -s <ip>:5555 shell run-as com.vbot_client.phicommr1 "cat /data/data/com.vbot_client.phicommr1/shared_prefs/vbot_r1.xml"
```

---

## 3. API HTTP nội bộ (Không cần đăng nhập)

Thay `IP_LOA` bằng địa chỉ IP của loa Phicomm R1. Các endpoint dưới đây có thể gọi trực tiếp từ mạng LAN (Home Assistant, Node-RED, Script...) mà không cần phiên đăng nhập WebUI:

### 3.1 Đọc văn bản tiếng Việt (TTS — hỗ trợ cả văn bản dài)

`POST /tts` — tham số: `text`, `lang` (mặc định `vi-VN`), `speed` (mặc định `1.0`).

```bash
curl -X POST \
  --data-urlencode "text=Xin chào gia đình, chúc một ngày tốt lành!" \
  -d "lang=vi-VN" \
  -d "speed=1.0" \
  "http://IP_LOA:8081/tts"
```

Cũng hỗ trợ truyền tham số trên URL:

```bash
curl -X POST "http://IP_LOA:8081/tts?text=ch%C3%A0o+b%E1%BA%A1n&lang=vi-VN&speed=1.0"
```

### 3.2 Phát chuông cảnh báo thông minh kèm lời đọc

`POST /alert` — tham số: `type` (`doorbell`, `chime`, `alarm`, `warning`, `security`, `fire`), `message`, `volume` (`10`–`100`), `repeats` (`1`–`5`), `ringtone` (ID/tên file MP3 tùy chọn).

```bash
curl -X POST \
  -d "type=doorbell" \
  --data-urlencode "message=Có khách đang bấm chuông ngoài cổng" \
  -d "volume=85" \
  "http://IP_LOA:8081/alert"
```

### 3.3 Phát URL âm thanh, YouTube hoặc Zing MP3

`POST /play_audio` — dùng chung trường `audioSource`:

```bash
# Phát URL MP3/AAC/M3U8 trực tiếp
curl -X POST --data-urlencode "audioSource=https://example.com/audio.mp3" "http://IP_LOA:8081/play_audio"

# Phát YouTube
curl -X POST --data-urlencode "audioSource=https://www.youtube.com/watch?v=VIDEO_ID" "http://IP_LOA:8081/play_audio"

# Phát Zing MP3
curl -X POST --data-urlencode "audioSource=https://zingmp3.vn/bai-hat/..." "http://IP_LOA:8081/play_audio"
```

### 3.4 Điều khiển phát & Truy vấn thông tin thiết bị

```bash
# Tạm dừng / Tiếp tục / Dừng phát
curl -X POST "http://IP_LOA:8081/pause_audio"
curl -X POST "http://IP_LOA:8081/continue_audio"
curl -X POST "http://IP_LOA:8081/stop_audio"

# Lấy thông tin trạng thái & định danh thiết bị (JSON)
curl "http://IP_LOA:8081/VBot_Client_Info"
```

---

## 4. Hướng dẫn cài đặt & Cập nhật APK (`PhicommR1_VBotClient.apk`)

### Cách 1: Cập nhật tự động ngay trên WebUI (Khuyên dùng khi đã cài đặt từ trước)
1. Mở `http://<IP_LOA>:8081` → mục **Kiểm Tra Cập Nhật**.
2. Nhấn **Kiểm Tra Phiên Bản Mới**, sau đó nhấn **Tự Động Tải & Cập Nhật Ngay** để loa tự tải `PhicommR1_VBotClient.apk` và nâng cấp tại chỗ.

### Cách 2: Sử dụng công cụ tự động `Update_R1.bat` trên Windows (Khuyên dùng khi cài mới)
1. Tải và giải nén trọn bộ thư mục `adb_tools_VBot_Client_PhicommR1_Auto_Install`.
2. Chạy file `Update_R1.bat`:
   - Chọn **`5. Cai dat / Cap nhat VBot Client (Socket) + Cap quyen Nut nhan`**, nhập địa chỉ IP của loa (ví dụ `192.168.1.50`). Công cụ sẽ tự động ẩn các app gốc gây xung đột của Phicomm, đẩy và cài đặt `PhicommR1_VBotClient.apk`, cấp quyền `WRITE_SECURE_SETTINGS`, kích hoạt dịch vụ nút nhấn cứng đỉnh loa (`VBotKeyAccessibilityService`) và khởi chạy ứng dụng.
   - Chọn **`8. Cap lai quyen Nut nhan cung dinh loa`** bất cứ khi nào cần kích hoạt lại riêng quyền nút nhấn mà không cần cài lại APK.

### Cách 3: Cài đặt thủ công qua lệnh ADB

```powershell
# 1. Kết nối tới loa qua ADB mạng LAN
adb connect <ip>:5555

# 2. (Khi cài lần đầu) Ẩn các ứng dụng gốc gây xung đột mic/âm thanh của Phicomm
adb -s <ip>:5555 shell /system/bin/pm hide com.phicomm.speaker.device
adb -s <ip>:5555 shell /system/bin/pm hide com.phicomm.speaker.otaservice
adb -s <ip>:5555 shell /system/bin/pm hide com.phicomm.speaker.productiontest
adb -s <ip>:5555 shell /system/bin/pm hide com.phicomm.speaker.bugreport
adb -s <ip>:5555 shell settings put secure install_non_market_apps 1

# 3. Đẩy gói APK lên loa và cài đặt / cập nhật
adb -s <ip>:5555 push apk\PhicommR1_VBotClient.apk /data/local/tmp/vbot.apk
adb -s <ip>:5555 shell /system/bin/pm install -r /data/local/tmp/vbot.apk
adb -s <ip>:5555 shell rm /data/local/tmp/vbot.apk

# 4. Cấp quyền hệ thống & kích hoạt nút nhấn cứng trên đỉnh loa
adb -s <ip>:5555 shell pm grant com.vbot_client.phicommr1 android.permission.WRITE_SECURE_SETTINGS
adb -s <ip>:5555 shell settings put secure enabled_accessibility_services com.vbot_client.phicommr1/com.vbot_client.phicommr1.VBotKeyAccessibilityService
adb -s <ip>:5555 shell settings put secure accessibility_enabled 1

# 5. Khởi chạy VBot Client
adb -s <ip>:5555 shell am start -n com.vbot_client.phicommr1/.MainActivity
```

---

## 5. Cấu trúc thư mục phát hành `Phicomm_R1`

- `apk/PhicommR1_VBotClient.apk`: Gói cài đặt chính thức phiên bản `28/11/2026-1` (dùng cho cài mới thủ công và cập nhật OTA trực tiếp từ WebUI).
- `adb_tools_VBot_Client_PhicommR1_Auto_Install/`: Bộ công cụ tự động trên Windows gồm `Update_R1.bat`, `PhicommR1_VBotClient.apk` mới nhất, `adb.exe`, gói nâng cấp Firmware OTA và các gói hỗ trợ hệ thống R1 (`unisound.apk`, `autodlna.apk`, `new_EchoService.apk`).
- `adb_tools.zip`: Bộ công cụ `adb` độc lập cho Windows khi chỉ cần chạy dòng lệnh thủ công.
- `Version.json`: Thông tin phiên bản (`build_date`: `28/11/2026-1`) và mô tả chi tiết bản cập nhật phục vụ kiểm tra OTA.

<img width="1902" height="1092" alt="Image" src="https://github.com/user-attachments/assets/363614f6-bc84-40e4-9ab7-e616e1a3532c" />
