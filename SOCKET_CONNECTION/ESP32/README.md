Yêu Cầu: Thiết Bị Raspberry Pi Chạy VBot Làm Server, Hoặc Sử Dung Loa Thông Minh Chạy VBot

# Hướng Dẫn Nạp Firmware ESP32 VBot Client - Chế Độ Socket

Tài liệu này hướng dẫn nạp firmware cho ESP32 VBot Client chạy ở chế độ Socket/WebSocket.

## 1. File cần chuẩn bị
	cần có các file `.bin` sau trong thư mục bin:
	bootloader.bin
	partitions.bin
	firmware.bin
	littlefs.bin

## 2. Thông tin phân vùng flash
	Firmware này sử dụng partition như sau:
	
	# Name,   Type, SubType, Offset,   Size,     Flags
	nvs,      data, nvs,     0x9000,   0x5000,
	otadata,  data, ota,     0xe000,   0x2000,
	app0,     app,  ota_0,   0x10000,  0x1A0000,
	app1,     app,  ota_1,   0x1B0000, 0x1A0000,
	littlefs, data, spiffs,  0x350000, 0xA0000,
	coredump, data, coredump,0x3F0000, 0x10000,

## 3. Bắt đầu tiến hành nạp firmware
	Mở phần mềm ESP Flash Download Tool và chọn:
	
		Chip Type: ESP32
		WorkMode: Develop
		LoadMode: UART

	Cấu hình khuyến nghị:
	
		SPI SPEED : 40MHz
		SPI MODE  : DIO
		FLASH SIZE: 4MB
		BAUD      : 921600

	Khi nạp bằng ESP Flash Download Tool hoặc esptool, cần điền đúng các giá trị và lần lượt thứ tự file, Offset:
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

## 4. Nạp bằng esptool dòng lệnh
	Có thể nạp đầy đủ bằng lệnh:
		python -m esptool --chip esp32 --port COM11 --baud 921600 write_flash -z ^
		0x1000 bootloader.bin ^
		0x8000 partitions.bin ^
		0x10000 firmware.bin ^
		0x350000 littlefs.bin

## 5. Erase flash khi cần
	Nếu đổi partition hoặc nạp bản mới hoàn toàn, nên xóa flash trước:
		python -m esptool --chip esp32 --port COM11 erase_flash

	Sau đó nạp lại đầy đủ các file .bin.

## 6. Sau khi nạp xong

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
		
	Khi kết nối thành công,cso thể kết nối xem logs Serial sẽ hiện tương tự:
		- Đã kết nối WebSocket
		- Gửi cấu hình tới máy chủ VBot
		- Đã nhận cấu hình Client

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

<img width="990" height="802" alt="Image" src="https://github.com/user-attachments/assets/1aa26732-6459-4e19-a5cb-9c441afe3a66" />
