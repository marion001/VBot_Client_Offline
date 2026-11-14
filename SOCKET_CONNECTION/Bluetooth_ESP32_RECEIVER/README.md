# VBot Bluetooth Audio Receiver (ESP32 Thường)

Module bộ thu âm thanh Bluetooth A2DP Sink (ESP32 Dev Module) hoạt động độc lập kết hợp cùng **VBot Client ESP32-S3**, giúp biến loa trợ lý ảo VBot thành một chiếc loa Bluetooth thông minh chất lượng cao với đầy đủ tính năng hiển thị và điều khiển.

---

## 1. Tính Năng Nổi Bật

1. **Âm thanh Bluetooth chất lượng cao (A2DP)**:
   - Thu âm thanh stereo 44.1kHz từ điện thoại, máy tính bảng, PC.
   - Truyền tín hiệu số PCM trực tiếp sang ESP32-S3 qua giao thức **I2S Master -> Slave RX**, đảm bảo chất lượng âm thanh thuần kỹ thuật số, không bị nhiễu analog.

2. **Giao tiếp hai chiều thông minh qua UART (115200 baud)**:
   - **Nhận cấu hình từ ESP32-S3**: Tự động nhận tên Bluetooth theo cài đặt Tên thiết bị (`cname`) trên WebUI của ESP32-S3. Nếu không cấu hình, mặc định sẽ là `VBot Client ESP32 [MAC]`.
   - **Bật/Tắt Bluetooth từ xa**: Tự động kích hoạt hoặc ngắt Bluetooth theo tùy chọn trên WebUI của ESP32-S3.
   - **Metadata đồng bộ thời gian thực**: Tự động trích xuất và gửi tiêu đề bài hát (**Title**), ca sĩ (**Artist**), album (**Album**) sang ESP32-S3 để hiển thị lên WebUI và Home Assistant.
   - **Tên thiết bị kết nối (Peer Name)**: Đọc thông tin thiết bị đang kết nối (tên iPhone, Samsung Galaxy, v.v.) qua Bluetooth GAP và đồng bộ về S3.
   - **Điều khiển phát nhạc (AVRCP)**: Hỗ trợ nhận các lệnh điều khiển `PLAY`, `PAUSE`, `NEXT`, `PREV`, `DISCONNECT` từ phím bấm cứng, WebUI VBot hoặc các Automation/Dashboard của Home Assistant.

3. **Cơ chế chuyển tiếp thông minh (Zero Collision với Mic & WakeNet)**:
   - Khi tạm dừng nhạc (Pause) hoặc không có tín hiệu âm thanh (>800ms), ESP32-S3 sẽ tự động chuyển nhượng phần cứng I2S để kích hoạt lại Microphone INMP441 và WakeNet ("Hi Lily", "VBot ơi").
   - Khi nhạc tiếp tục phát, hệ thống tự động tạm ngưng Mic và chuyển DAC Loa sang 44.1kHz Stereo tức thì.

---

## 2. Sơ Đồ Đấu Dây (Pinout Wiring)

Kết nối giữa **ESP32 Thường** và **ESP32-S3 VBot Client**:

| Tín hiệu | ESP32 Thường (A2DP Receiver) | ESP32-S3 (VBot Client) | Ghi chú |
| :--- | :--- | :--- | :--- |
| **I2S BCLK** | **GPIO 14** | **GPIO 14** | Xung bit clock I2S |
| **I2S WS (LRCK)** | **GPIO 21** | **GPIO 21** | Xung chọn kênh L/R |
| **I2S DATA OUT** | **GPIO 22** | **GPIO 47 (DIN)** | Dữ liệu âm thanh PCM |
| **UART TX** | **GPIO 17 (TX2)** | **GPIO 2 (RX)** | ESP32 gửi metadata & trạng thái |
| **UART RX** | **GPIO 16 (RX2)** | **GPIO 1 (TX)** | ESP32 nhận cấu hình & lệnh điều khiển |
| **GND** | **GND** | **GND** | **Bắt buộc nối chung Mass (GND)** |
| **Nguồn cấp** | **5V (VIN)** | **5V (VIN)** | Khuyên dùng nguồn 5V 2A-3A chung |

---

## 3. Danh Mục Tệp Nhị Phân (.bin)

Tất cả các tệp nhị phân cần thiết đã được biên dịch sẵn trong thư mục `bin/`:

* `bin/bootloader.bin` (Địa chỉ: `0x1000`)
* `bin/partitions.bin` (Địa chỉ: `0x8000`)
* `bin/boot_app0.bin` (Địa chỉ: `0xe000`)
* `bin/firmware.bin` (Địa chỉ: `0x10000`)

---

## 4. Hướng Dẫn Nạp Flash

### Cách 1: Sử dụng Flash Download Tool (Giao diện đồ họa)
1. Mở phần mềm **ESP Flash Download Tool** (Espressif).
2. Chọn ChipType: **ESP32**, WorkMode: **Develop**.
3. Điền đường dẫn các file và địa chỉ offset như sau:
   - `bin\bootloader.bin` -----> `0x1000`
   - `bin\partitions.bin` -----> `0x8000`
   - `bin\boot_app0.bin` -----> `0xe000`
   - `bin\firmware.bin` -----> `0x10000`
4. Cấu hình thông số SPI:
   - **SPI SPEED**: `40MHz` hoặc `80MHz`
   - **SPI MODE**: `DIO` (hoặc `QIO`)
   - **FLASH SIZE**: `32Mbit` (4MB)
5. Chọn đúng cổng COM của ESP32 Thường, chọn Baudrate `921600` rồi nhấn **START**.

### Cách 2: Sử dụng lệnh esptool.py (Command Line)
```bash
esptool.py --chip esp32 --port COMx --baud 921600 --before default_reset --after hard_reset write_flash -z \
  --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x1000 bin/bootloader.bin \
  0x8000 bin/partitions.bin \
  0xe000 bin/boot_app0.bin \
  0x10000 bin/firmware.bin
```
*(Thay `COMx` bằng cổng COM thực tế của máy tính)*.

---

## 5. Kiểm Tra Hoạt Động

1. Cấp nguồn cho cả 2 chip và nối chung chân GND, I2S và UART theo bảng sơ đồ chân.
2. Mở WebUI của VBot Client ESP32-S3:
   - Vào tab **Cấu Hình GPIO (LED, MIC, Bluetooth, Audio)** để kiểm tra các chân GPIO Bluetooth I2S & UART.
   - Vào tab **Bluetooth Control** để theo dõi trạng thái kết nối, tên thiết bị đang kết nối và điều khiển bài hát.
3. Trên điện thoại, dò Bluetooth và kết nối với tên loa hiển thị trên WebUI. Khi phát nhạc, âm thanh sẽ tự động phát ra loa ngoài MAX98357 qua ESP32-S3.
