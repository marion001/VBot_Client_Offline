Yêu Cầu: Thiết Bị Raspberry Pi Chạy VBot Làm Server, Hoặc Sử Dung Loa Thông Minh Chạy VBot


Lưu ý: Các chân GPIO LED có thể sử dụng: 

	  Với ESP32s3 sử dụng các chân GPIO:
		- GPIO0 -> GPIO21
	   	- GPIO26
	   	- GPIO33 -> GPIO48
     
	  Với ESP32 sử dụng các chân GPIO:
		- GPIO0->GPIO5
		- GPIO11->GPIO19
		- GPIO21->GPIO23
		- GPIO25->GPIO27
  		- GPIO32
		- GPIO33

- VBot Offline: https://github.com/marion001/VBot_Offline
- B1: Cập Nhật Chương Trình Và Giao Diện VBot Lên Bản Mới Nhất
- B2: Trong Giao Diện WebUI VBot Đi Tới:
     + Cấu Hình Config -> Streming Audio Server -> Kích Hoạt (Bật Lên)
     + Chọn Kiểu Loại Kết Nối Là: Sử dụng ESP32, ESP32 D1 Mini, ESP32S3, ESP32S3 Supper Mini
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
   
   	+ Với ESP32 làm theo hướng dẫn sau: 
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


	+ Với ESP32s3 làm theo hướng dẫn sau: 
	  	- Chọn: (Chiptype = ESP32s3) -> (WorkMode = Develop) -> -> (LoadMode = UART) -> OK
	  	- Ở phần Download Path Config chọn lần lượt thứ tự và tên file như sau:
	  		+ Thêm file esp32s3_udp_stream.ino.bootloader.bin với địa chỉ 0x0
	  		+ Thêm file esp32s3_udp_stream.ino.partitions.bin với địa chỉ 0x8000
	  		+ Thêm file esp32s3_udp_stream.ino.bin với địa chỉ 0x10000
	  	- Tích Chọn Tiếp Các Mục Sau:
	  		+ SPI SPEED = 80MHz
	  		+ SPI MODE = QIO
	  		+ COM = (Chọn cổng com mà ESP32s3 kết nối)
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
   
![Image](https://github.com/user-attachments/assets/31df2568-ccbd-4a4f-95ca-d0a2180eca35)
![Image](https://github.com/user-attachments/assets/a4600a0f-54dd-4e89-961a-caf29b9ba95a)
![Image](https://github.com/user-attachments/assets/cbe69c43-8473-4594-a568-7d18ad2165ee)

![Image](https://github.com/user-attachments/assets/de9d1bcd-64a4-4e79-94a5-0d3f621e0349)
