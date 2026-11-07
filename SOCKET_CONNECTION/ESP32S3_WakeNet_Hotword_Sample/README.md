# Thư Viện Mẫu WakeUp Hotword WakeNet9 Cho ESP32-S3

Thư mục này chứa toàn bộ các tệp nhị phân mô hình nhận diện từ khóa đánh thức **WakeNet9 (On-Device Wake Word Engine)** chính thức từ Espressif, đã được đóng gói sẵn thành định dạng `.bin` độc lập để nạp trực tiếp qua WebUI của ESP32-S3 VBot Client.

---

## 1. Giới thiệu WakeNet9 trên ESP32-S3

- **WakeNet9** là kiến trúc mạng nơ-ron nhận diện giọng nói đánh thức tiên tiến nhất của Espressif, được tối ưu hóa cho tập lệnh tăng tốc phần cứng vector của ESP32-S3.
- **Hoạt động Offline 100%**: Xử lý trực tiếp trên phần cứng ESP32-S3 với độ trễ phản hồi tức thì (~0ms).
- **Tiết kiệm tài nguyên mạng**: Ở trạng thái chờ đánh thức, ESP32 không cần gửi stream âm thanh liên tục qua Wi-Fi về máy chủ, giúp giảm tải băng thông và máy chủ VBot.

---

## 2. Danh sách 18 mô hình WakeNet9 có sẵn trong thư mục

Bạn có thể tải về bất kỳ file `.bin` nào dưới đây để nạp vào thiết bị qua WebUI:

| STT | Từ khóa đánh thức (Wake Word) | Tên mô hình (Model Key) | Tệp nhị phân (.bin) | Dung lượng |
|:---:|---|---|---|:---:|
| 1 | **Alexa** | `wn9_alexa` | [`wn9_alexa.bin`](wn9_alexa.bin) | ~284 KB |
| 2 | **Astrolabe** | `wn9_astrolabe_tts` | [`wn9_astrolabe_tts.bin`](wn9_astrolabe_tts.bin) | ~284 KB |
| 3 | **BlueChip** | `wn9_bluechip_tts2` | [`wn9_bluechip_tts2.bin`](wn9_bluechip_tts2.bin) | ~284 KB |
| 4 | **Computer** | `wn9_computer_tts` | [`wn9_computer_tts.bin`](wn9_computer_tts.bin) | ~284 KB |
| 5 | **Hey Ily** | `wn9_heyily_tts2` | [`wn9_heyily_tts2.bin`](wn9_heyily_tts2.bin) | ~284 KB |
| 6 | **Hey Ivy** | `wn9_heyivy_tts2` | [`wn9_heyivy_tts2.bin`](wn9_heyivy_tts2.bin) | ~284 KB |
| 7 | **Hey Kira** | `wn9_heykira_tts3` | [`wn9_heykira_tts3.bin`](wn9_heykira_tts3.bin) | ~284 KB |
| 8 | **Hey Wanda** | `wn9_heywanda_tts` | [`wn9_heywanda_tts.bin`](wn9_heywanda_tts.bin) | ~284 KB |
| 9 | **Hey Willow** | `wn9_heywillow_tts` | [`wn9_heywillow_tts.bin`](wn9_heywillow_tts.bin) | ~284 KB |
| 10 | **Hi Andy** | `wn9_hiandy_tts2` | [`wn9_hiandy_tts2.bin`](wn9_hiandy_tts2.bin) | ~284 KB |
| 11 | **Hi ESP** | `wn9_hiesp` | [`wn9_hiesp.bin`](wn9_hiesp.bin) | ~284 KB |
| 12 | **Hi Jason** | `wn9_hijason_tts2` | [`wn9_hijason_tts2.bin`](wn9_hijason_tts2.bin) | ~284 KB |
| 13 | **Hi Joy** | `wn9_hijoy_tts` | [`wn9_hijoy_tts.bin`](wn9_hijoy_tts.bin) | ~284 KB |
| 14 | **Hi Lily** | `wn9_hilili_tts` | [`wn9_hilili_tts.bin`](wn9_hilili_tts.bin) | ~284 KB |
| 15 | **Hi M Five** | `wn9_himfive` | [`wn9_himfive.bin`](wn9_himfive.bin) | ~284 KB |
| 16 | **Jarvis** | `wn9_jarvis_tts` | [`wn9_jarvis_tts.bin`](wn9_jarvis_tts.bin) | ~284 KB |
| 17 | **Mycroft** | `wn9_mycroft_tts` | [`wn9_mycroft_tts.bin`](wn9_mycroft_tts.bin) | ~284 KB |
| 18 | **Sophia** | `wn9_sophia_tts` | [`wn9_sophia_tts.bin`](wn9_sophia_tts.bin) | ~284 KB |

---

## 3. Cách nạp và sử dụng Hotword trên WebUI

1. Mở trình duyệt và truy cập địa chỉ IP của ESP32-S3 (ví dụ: `http://192.168.14.147`).
2. Chuyển sang tab **`Cấu Hình Hotword WakeUP`**.
3. Tại mục **Chế độ Hotword / Đánh thức**, chọn **`WakeNet On-Device (ESP32-S3)`**.
4. Cuộn xuống phần **Quản Lý Hotword On-Device (ESP32-S3 WakeNet)**:
   - Nhấn **Chọn tệp** và chọn file `.bin` bạn đã tải về (ví dụ: `wn9_alexa.bin` hoặc `wn9_jarvis_tts.bin`).
   - Nhập **Tên hiển thị gợi nhớ** (tùy chọn, ví dụ: `Trợ Lý Jarvis`, `Alexa`).
   - Nhấn nút **Nạp Hotword**.
5. Sau khi nạp thành công:
   - Từ khóa mới sẽ xuất hiện trong bảng danh sách hotword tùy chỉnh (hỗ trợ tối đa **5 slot tùy chỉnh**).
   - Chọn từ khóa vừa nạp tại mục dropdown **`Từ khóa WakeNet On-Device`**.
   - Nhấn **Lưu cấu hình & khởi động lại**.

> [!TIP]
> - **Chính sách bảo vệ**: Chỉ những hotword do người dùng tải lên mới có nút **Xóa**. 4 hotword mặc định (*Hi Lily*, *Alexa*, *Sophia*, *Jarvis*) được tích hợp sẵn trên phân vùng Flash và luôn cố định để làm phương án dự phòng an toàn.
> - Dung lượng hỗ trợ tải lên file `.bin` tối đa lên tới **2MB**.
