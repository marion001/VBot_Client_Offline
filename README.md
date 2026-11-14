# VBot Client Offline

Hệ thống Client thông minh dành riêng cho **VBot Assistant (Loa Chủ)**, hỗ trợ kết nối qua chuẩn **WebSocket (Socket Connection)**, tích hợp nhận diện từ khóa đánh thức WakeNet On-Device, module thu Bluetooth A2DP âm thanh nổi, giao diện WebUI điều khiển hiện đại và đồng bộ hoàn toàn với **Home Assistant**.

---

## 1. Cấu Trúc Thư Mục Dự Án

* 📁 [**SOCKET_CONNECTION/**](SOCKET_CONNECTION/): Thư mục chứa toàn bộ firmware nhị phân (.bin), bảng phân vùng và tài liệu cho 9 biến thể ESP32 / ESP32-S3:
  * `ESP32` / `ESP32_WakeNet`: Dành cho ESP32 WROOM / WROVER / Dev Module.
  * `ESP32S3`: Dành cho ESP32-S3 Flash 8MB (không PSRAM).
  * `ESP32S3_N8R2` / `ESP32S3_N8R2_WakeNet`: ESP32-S3 Flash 8MB + 2MB PSRAM QSPI.
  * `ESP32S3_N8R8` / `ESP32S3_N8R8_WakeNet`: ESP32-S3 Flash 8MB + 8MB PSRAM OPI.
  * `ESP32S3_N16R8` / `ESP32S3_N16R8_WakeNet`: ESP32-S3 Flash 16MB + 8MB PSRAM OPI (Hỗ trợ WakeNet đa từ khóa offline).
  * 🎵 [**Bluetooth_ESP32_RECEIVER/**](SOCKET_CONNECTION/Bluetooth_ESP32_RECEIVER/): **Module bộ thu âm thanh Bluetooth A2DP Sink (ESP32 Thường)** kết nối qua I2S Slave và UART với ESP32-S3, kèm file nạp `.bin` và sơ đồ đấu nối chi tiết.
  * 📁 `Phicomm_R1`: Client dành riêng cho loa thông minh Phicomm R1.
  * 📁 `SOURCE/ESP32`: Toàn bộ mã nguồn hoàn chỉnh C++/Arduino/PlatformIO và giao diện WebUI.

---

## 2. Các Tính Năng Nổi Bật (Phiên bản 14/11/2026)

1. **Module Thu Bluetooth A2DP & Điều Khiển Toàn Diện**:
   - Nhận nhạc chất lượng cao từ điện thoại/PC qua ESP32 thường, truyền tín hiệu số PCM sang ESP32-S3 qua **I2S Slave RX** (44.1kHz Stereo).
   - Truyền metadata bài hát (**Title, Artist, Album**), tên thiết bị kết nối (**Peer Device Name**) và các lệnh điều khiển (**Play, Pause, Next, Prev, Disconnect**) qua UART hai chiều.
   - Tab điều khiển **Bluetooth Control** trên WebUI với giao diện trực quan và phản hồi tức thì.
   - Cơ chế giải phóng phần cứng thông minh: Tự động trả lại I2S cho Microphone INMP441 và WakeNet khi nhạc tạm dừng (>800ms im lặng).

2. **Nhận Diện Giọng Nói Offline (WakeNet9 On-Device)**:
   - Xử lý nhận diện từ khóa đánh thức hoàn toàn Offline (~0ms độ trễ), không tốn băng thông Wi-Fi và hoạt động độc lập khi ở trạng thái chờ.
   - Hỗ trợ nạp và quản lý trực tiếp tới **5 từ khóa tùy chỉnh** thông qua WebUI.

3. **Cấu Hình Phần Cứng Linh Hoạt (Tab GPIO)**:
   - Tab **Cấu Hình GPIO (LED, MIC, Bluetooth, Audio)** hỗ trợ cấu hình tùy biến mọi chân GPIO: LED WS2812, nút bấm, rotary encoder, Microphone I2S, DAC Loa MAX98357 và Bluetooth I2S & UART.

4. **Đồng Bộ Hoàn Hảo Với Home Assistant**:
   - Tự động xuất bản các cảm biến trạng thái Bluetooth, thiết bị kết nối, trạng thái phát và tên bài hát lên MQTT.
   - Điều khiển media_player chuẩn Home Assistant (`script/media_control/set` và `script/bluetooth_control/set`).

---

## 3. Liên Kết Hữu Ích

* [Tài Liệu Hướng Dẫn Cài Đặt SOCKET_CONNECTION](SOCKET_CONNECTION/README.md)
* [Hướng Dẫn Nạp Flash Cho Tất Cả Các Dòng Chip](SOCKET_CONNECTION/README_FLASH.md)
* [Hướng Dẫn Module Bluetooth ESP32 RECEIVER](SOCKET_CONNECTION/Bluetooth_ESP32_RECEIVER/README.md)
* [Trang Chính VBot Assistant (Loa Chủ)](https://github.com/marion001/VBot_Offline)
