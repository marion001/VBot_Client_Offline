# Thư Viện Mô Hình WakeNet9 Cho ESP32-S3 VBot Client

Thư mục này chứa các tệp nhị phân mô hình nhận diện từ khóa đánh thức **WakeNet9 (On-Device Wake Word Engine)** dành riêng cho ESP32-S3 có PSRAM.

---

## 1. Giới thiệu WakeNet9 trên ESP32-S3

- **WakeNet9** là kiến trúc mạng nơ-ron nhận diện giọng nói đánh thức tiên tiến nhất của Espressif, được tối ưu hóa cho tập lệnh vector của ESP32-S3.
- **Hoạt động Offline 100%**: Xử lý trực tiếp trên phần cứng ESP32-S3 với độ trễ phản hồi tức thì (~0ms).
- **Tiết kiệm tài nguyên**: Ở trạng thái chờ đánh thức, ESP32 không cần gửi stream âm thanh liên tục qua Wi-Fi về máy chủ, giúp giảm tải mạng và máy chủ VBot.

---

## 2. Danh sách 18 mô hình WakeNet9 chính thức (Đã đóng gói `.bin`)

Tất cả 18 mô hình WakeNet9 chính thức của Espressif đã được đóng gói sẵn thành các tệp nhị phân `.bin` độc lập trong thư mục [`models/wakenet_bin_models/`](wakenet_bin_models/):

| STT | Từ khóa đánh thức (Wake Word) | Tên mô hình (Model Key) | Tệp nhị phân (.bin) | Dung lượng |
|:---:|---|---|---|:---:|
| 1 | **Alexa** | `wn9_alexa` | [`wn9_alexa.bin`](wakenet_bin_models/wn9_alexa.bin) | ~284 KB |
| 2 | **Astrolabe** | `wn9_astrolabe_tts` | [`wn9_astrolabe_tts.bin`](wakenet_bin_models/wn9_astrolabe_tts.bin) | ~284 KB |
| 3 | **BlueChip** | `wn9_bluechip_tts2` | [`wn9_bluechip_tts2.bin`](wakenet_bin_models/wn9_bluechip_tts2.bin) | ~284 KB |
| 4 | **Computer** | `wn9_computer_tts` | [`wn9_computer_tts.bin`](wakenet_bin_models/wn9_computer_tts.bin) | ~284 KB |
| 5 | **Hey Ily** | `wn9_heyily_tts2` | [`wn9_heyily_tts2.bin`](wakenet_bin_models/wn9_heyily_tts2.bin) | ~284 KB |
| 6 | **Hey Ivy** | `wn9_heyivy_tts2` | [`wn9_heyivy_tts2.bin`](wakenet_bin_models/wn9_heyivy_tts2.bin) | ~284 KB |
| 7 | **Hey Kira** | `wn9_heykira_tts3` | [`wn9_heykira_tts3.bin`](wakenet_bin_models/wn9_heykira_tts3.bin) | ~284 KB |
| 8 | **Hey Wanda** | `wn9_heywanda_tts` | [`wn9_heywanda_tts.bin`](wakenet_bin_models/wn9_heywanda_tts.bin) | ~284 KB |
| 9 | **Hey Willow** | `wn9_heywillow_tts` | [`wn9_heywillow_tts.bin`](wakenet_bin_models/wn9_heywillow_tts.bin) | ~284 KB |
| 10 | **Hi Andy** | `wn9_hiandy_tts2` | [`wn9_hiandy_tts2.bin`](wakenet_bin_models/wn9_hiandy_tts2.bin) | ~284 KB |
| 11 | **Hi ESP** | `wn9_hiesp` | [`wn9_hiesp.bin`](wakenet_bin_models/wn9_hiesp.bin) | ~284 KB |
| 12 | **Hi Jason** | `wn9_hijason_tts2` | [`wn9_hijason_tts2.bin`](wakenet_bin_models/wn9_hijason_tts2.bin) | ~284 KB |
| 13 | **Hi Joy** | `wn9_hijoy_tts` | [`wn9_hijoy_tts.bin`](wakenet_bin_models/wn9_hijoy_tts.bin) | ~284 KB |
| 14 | **Hi Lily** | `wn9_hilili_tts` | [`wn9_hilili_tts.bin`](wakenet_bin_models/wn9_hilili_tts.bin) | ~284 KB |
| 15 | **Hi M Five** | `wn9_himfive` | [`wn9_himfive.bin`](wakenet_bin_models/wn9_himfive.bin) | ~284 KB |
| 16 | **Jarvis** | `wn9_jarvis_tts` | [`wn9_jarvis_tts.bin`](wakenet_bin_models/wn9_jarvis_tts.bin) | ~284 KB |
| 17 | **Mycroft** | `wn9_mycroft_tts` | [`wn9_mycroft_tts.bin`](wakenet_bin_models/wn9_mycroft_tts.bin) | ~284 KB |
| 18 | **Sophia** | `wn9_sophia_tts` | [`wn9_sophia_tts.bin`](wakenet_bin_models/wn9_sophia_tts.bin) | ~284 KB |

---

## 3. Cách nạp và sử dụng Hotword trên WebUI

1. Mở trình duyệt và truy cập địa chỉ IP của ESP32-S3 (ví dụ: `http://192.168.14.147`).
2. Vào tab **`Cấu Hình Hotword WakeUP`**.
3. Tại ô **Chế độ Hotword / Đánh thức**, chọn **`WakeNet On-Device (ESP32-S3)`**.
4. Cuộn xuống phần **Quản Lý Hotword On-Device (ESP32-S3 WakeNet)**:
   - Nhấn **Chọn tệp** và chọn file `.bin` bất kỳ trong danh sách ở trên (ví dụ `wn9_alexa.bin` hoặc `wn9_jarvis_tts.bin`).
   - Nhập tên hiển thị gợi nhớ (tùy chọn, ví dụ: `Trợ Lý Jarvis`, `Alexa`).
   - Nhấn nút **Nạp Hotword**.
5. Sau khi nạp thành công:
   - Từ khóa mới sẽ xuất hiện trong bảng danh sách hotword tùy chỉnh.
   - Chọn từ khóa vừa nạp tại mục dropdown **`Từ khóa WakeNet On-Device`**.
   - Nhấn **Lưu cấu hình & khởi động lại**.

> [!NOTE]
> - Thiết bị hỗ trợ lưu trữ tối đa **5 hotword tùy chỉnh** của người dùng.
> - **Chính sách bảo mật**: Chỉ những hotword do người dùng tải lên mới có nút **Xóa**. 4 hotword mặc định (*Hi Lily*, *Alexa*, *Sophia*, *Jarvis*) được tích hợp sẵn trên phân vùng Flash và được khóa cố định nhằm đảm bảo thiết bị luôn có từ khóa hoạt động dự phòng an toàn.
> - Nếu xóa một hotword đang được chọn hoạt động, hệ thống sẽ tự động chuyển về từ khóa mặc định `Hi Lily (wn9_hilili_tts)`.

---

## 4. Hướng dẫn tự đóng gói mô hình WakeNet thành file `.bin`

Nếu bạn có một mô hình WakeNet tùy chỉnh (tự train hoặc từ Espressif) dưới dạng thư mục chứa các file:
- `wn9_data`
- `wn9_index`
- `_MODEL_INFO_`

Bạn có thể dễ dàng đóng gói thành 1 file `.bin` duy nhất để tải lên WebUI bằng script Python có sẵn [`scripts/pack_custom_hotword.py`](../scripts/pack_custom_hotword.py):

```powershell
python scripts/pack_custom_hotword.py -d "duong_dan_thu_muc_model" -o "models/wakenet_bin_models/ten_model.bin" -n "wn9_ten_model"
```

**Ví dụ:**
```powershell
python scripts/pack_custom_hotword.py -d "C:/model/wn9_vbot" -o "models/wakenet_bin_models/wn9_vbot.bin" -n "wn9_vbot"
```

Sau khi đóng gói xong, bạn chỉ cần chọn file `.bin` đó trên WebUI để nạp vào ESP32-S3.

---

## 5. Cấu trúc phân vùng Flash chứa mô hình mặc định (`srmodels.bin`)

Trên bo mạch ESP32-S3 N16R8 (16MB Flash), bảng phân vùng `partitions_vbot_ota_16mb.csv` dành riêng một phân vùng dữ liệu thô `model` (4MB):

```csv
# Name,   Type, SubType, Offset,   Size, Flags
model,    data, ,        0xC00000, 0x3F0000,
```

Tệp `models/srmodels.bin` (dung lượng ~1.11 MB) chứa sẵn 4 mô hình WakeNet9 mặc định (*Hi Lily*, *Alexa*, *Sophia*, *Jarvis*).

### Lệnh nạp `srmodels.bin` xuống Flash tại offset `0xC00000`:

Bằng công cụ `esptool.py` của PlatformIO:
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py" --chip esp32s3 --port COM6 write_flash 0xC00000 models/srmodels.bin
```

Hoặc qua **Espressif Flash Download Tool**:
- File: `models/srmodels.bin`
- Offset: `0xC00000`
