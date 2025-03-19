Yêu Cầu: Thiết Bị Raspberry Pi Chạy VBot Làm Server, Hoặc Sử Dung Loa Thông Minh Chạy VBot

- VBot Offline: https://github.com/marion001/VBot_Offline
- B1: Cập Nhật Chương Trình Và Giao Diện VBot Lên Bản Mới Nhất
- B2: Trong Giao Diện WebUI VBot Đi Tới:
     + Cấu Hình Config -> Streming Audio Server -> Kích Hoạt (Bật Lên)
     + Chọn Kiểu Loại Kết Nối Là: UDP Socket
     + Lưu Cấu Hình -> Khởi Động Lại Chương Trình VBot

Phần Cứng Client: 

	  - ESP32 (ESP32, ESP32 Mini Wemos D1, V..v...)
	  - Mic i2s (INMP441, GY-SPH0645)
	  - Module Loa (MAX98357)
	  - LED (WS2812B)
	  - Nguồn 5V-2A
	  - Loa 3W 4-8ôm


Hướng Dẫn Flash VBot Client Sử Dụng ESP32

  1: Chuẩn Bị Công Cụ
  
	  - Link Tải Phần Mềm Flash Của espressif: https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32/production_stage/tools/flash_download_tool.html
	  - Kết nối ESP32 với máy tính qua cáp USB.


2: Cài Đặt Flash Download Tool

  	- Mở phần mềm flash_download_tool lên
  	- Chọn: (Chiptype = ESP32) -> (WorkMode = Develop) -> OK
  	- Ở phần Download Path Config chọn lần lượt thứ tự và tên file như sau:
  		+ Thêm file esp32_udp_stream.ino.bootloader.bin với địa chỉ 0x1000
  		+ Thêm file esp32_udp_stream.ino.partitions.bin với địa chỉ 0x8000
  		+ Thêm file esp32_udp_stream.ino.bin với địa chỉ 0x10000
  	- Tích Chọn Tiếp Các Mục Sau:
  		+ SPI SPEED = 80MHz
  		+ SPI MODE = QIO
  		+ COM = (Chọn cổng com mà ESP32 kết nối)
  		+ BAUD = 921600 (hoặc chọn 115200 tốc độ nạp chậm hơn)
  	- Nhấn nút Start để bắt đầu Nạp Chương Trình

 
Cấu Hình Client:

  - Truy Cập Vào WebUI Của Client Bằng Địa Chỉ IP
  - Cấu Hình Kết Nối Tới Server:
    + Thay Địa Chỉ IP Và Cổng PORT Tương Ứng Của Loa Đang Chạy VBot
    + 1 Số Cấu Hình Chân GPIO Cho Mic, Loa, LED Khác Sẽ nằm Bên Dưới
      
  Lưu Ý: 
  
  	- Khi Flash Xong Dùng Nguồn CỔng Từ USB Sẽ Bị Thiếu Nguồn Khiến ESP Bị RESET Liên Tục
  	- Nên Dùng Nguồn 5V-2A trở lên Để LED Được Sáng Ổn Định
   
![Image](https://github.com/user-attachments/assets/58555a36-8741-4e51-821a-fd10ef09c02a)
  
  
![Image](https://github.com/user-attachments/assets/4c65d912-a46f-44ad-b9d1-ef0cf4793646)


![Image](https://github.com/user-attachments/assets/de9d1bcd-64a4-4e79-94a5-0d3f621e0349)
