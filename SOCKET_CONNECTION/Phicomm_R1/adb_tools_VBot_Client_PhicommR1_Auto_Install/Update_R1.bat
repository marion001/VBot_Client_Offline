@echo off
setlocal
title Update Phicomm R1 -
cd /d %~dp0

:menu
cls
echo.
echo Update cho loa Phicomm R1 VBot Client Socket
echo.
echo -------------------------------------------------------
echo Vui long chon:
echo.
echo 1. Kiem tra Firmware loa hien tai
echo 2. Cap nhat firmware
echo 3. Xoa bo nho dem
echo 4. Cai dat Auto DLNA + Unisound (Fix am thanh)
echo 5. Cai dat VBot Client (Socket)
echo 6. Exit
echo -------------------------------------------------------
echo.
set /p choice= Chon thao tac ban muon thuc hien:
IF NOT "%Choice%"=="" SET Choice=%Choice:~0,1%
if /i "%choice%"=="1" goto check_fw
if /i "%choice%"=="2" goto update
if /i "%choice%"=="3" goto clear_cache
if /i "%choice%"=="4" goto install_apk
if /i "%choice%"=="5" goto install_ai
if /i "%choice%"=="6" goto exit
echo.
echo Lua chon khong hop le, vui long chon lai!
goto menu

:connect
color 03
if not exist adb.exe goto adbfile_not
set /p ip=Vui long nhap dia chi IP cua loa:
if "%ip%" == "" color 04&echo Dia chi IP khong duoc de trong!&choice /t 1 /d y /n > nul&goto connect
goto connect_ip

:connect_ip
color 06
echo.
echo Bat dau ket noi thiet bi...
choice /t 1 /d y /n > nul
adb disconnect > nul 2>&1
taskkill /f /t /im adb.exe > nul 2>&1
adb kill-server > nul 2>&1
adb connect %ip%
goto connect_ip_query

:connect_ip_query
color 06
echo.
echo Kiem tra trang thai thiet bi...
adb devices > nul
choice /t 1 /d y /n > nul
(for /f "tokens=1 delims=" %%i in ('adb -s %ip% get-state') do (
if "%%i" == "device" set type_id=true&set type=-s %ip%&goto test_connect
))
if "%query%" neq "true" echo Ket noi that bai, vui long nhap lai dia chi IP chinh xac!&choice /t 1 /d y /n > nul&adb kill-server
set query=
goto connect

:run_error
color 04
echo Da xay ra loi, Hay nhan phim bat ky de thu ket noi lai
pause > nul
goto connect

:test_connect
color 06
echo Dang kiem tra ket noi, vui long cho...
adb shell ls > nul
if %errorlevel%==1 goto run_error
color 03
echo Ket noi thanh cong...
goto :eof

:device_info
echo.
(for /f %%i in ('adb shell getprop ro.serialno') do (
set serialno=%%i
))

(for /f %%i in ('adb shell getprop ro.build.version.incremental') do (
set ver=%%i
))

(for /f %%i in ('adb shell getprop dhcp.wlan0.ipaddress') do (
set ipaddress=%%i
))

(for /f "tokens=1,2,3,4,5,6 delims= " %%i in ('adb shell getprop ro.build.host') do (
set tmp_text=%%i
if "%%j" neq "" set tmp_text=%tmp_text% %%j
if "%%k" neq "" set tmp_text=%tmp_text% %%k
if "%%l" neq "" set tmp_text=%tmp_text% %%l
if "%%m" neq "" set tmp_text=%tmp_text% %%m
if "%%n" neq "" set tmp_text=%tmp_text% %%n
))
set build_host=%tmp_text%

(for /f  "tokens=1,2,3,4,5,6 delims= " %%i in ('adb shell getprop ro.product.model') do (
set tmp_text=%%i
if "%%j" neq "" set tmp_text=%tmp_text% %%j
if "%%k" neq "" set tmp_text=%tmp_text% %%k
if "%%l" neq "" set tmp_text=%tmp_text% %%l
if "%%m" neq "" set tmp_text=%tmp_text% %%m
if "%%n" neq "" set tmp_text=%tmp_text% %%n
))
set build_model=%tmp_text%

(for /f "tokens=1,2,3,4,5,6 delims= " %%i in ('adb shell getprop net.hostname') do (
set tmp_text=%%i
if "%%j" neq "" set tmp_text=%tmp_text% %%j
if "%%k" neq "" set tmp_text=%tmp_text% %%k
if "%%l" neq "" set tmp_text=%tmp_text% %%l
if "%%m" neq "" set tmp_text=%tmp_text% %%m
if "%%n" neq "" set tmp_text=%tmp_text% %%n
))
set hostname=%tmp_text%

if "%build_host%" == "phicomm" if "%build_model%" == "rk322x-box" set r1=true

color 03
if "%hostname%" neq "" echo Device name: %hostname%
if "%ver%" neq "" echo Firmware version: %ver%
if "%ipaddress%" neq "" echo Device IP: %ipaddress%
if "%serialno%" neq "" echo Device SN: %serialno%
if %ver% gtr 2999 if %ver% neq 3448 (
color 06
if "%r1%" == "true" goto r1_low_ver
)
goto :eof

:r1_low_ver
echo.
echo Luu y: Phien ban firmware R1 cua ban khong phai la phien ban moi nhat. Ban co the nang cap Firmware bang cong cu nay!
echo.
echo Nhan phim bat ky de tiep tuc!
pause > nul
goto menu

:exit
echo.
echo Nhan phim bat ky de thoat!
pause > nul
exit

:connect_fail
echo.
echo Ket noi that bai, vui long thu lai!
pause > nul
goto connect

:check_fw
echo.
echo --------------------------------------------
echo        Kiem tra Firmware loa hien tai
echo --------------------------------------------
echo.
call :connect
call :device_info
echo.
echo Nhan phim bat ky de tiep tuc!
pause > nul
goto menu

:hide_packages
echo.
echo Tat cac ung dung khong can thiet tren loa:
adb shell /system/bin/pm hide com.phicomm.speaker.airskill
adb shell /system/bin/pm hide com.phicomm.speaker.exceptionreporter
adb shell /system/bin/pm hide com.phicomm.speaker.systemtool
adb shell /system/bin/pm hide com.phicomm.speaker.device
adb shell /system/bin/pm hide com.phicomm.speaker.otaservice
adb shell /system/bin/pm hide com.phicomm.speaker.productiontest
adb shell /system/bin/pm hide com.phicomm.speaker.bugreport
goto :eof

:unhide_packages
echo.
echo Dang khoi phuc ung dung can thiet:
adb shell /system/bin/pm unhide com.phicomm.speaker.ijetty
adb shell /system/bin/pm unhide com.phicomm.speaker.netctl
goto :eof


:install_ai
adb shell pm grant com.vbot_client.phicommr1 android.permission.WRITE_SECURE_SETTINGS
echo.
echo ---------------------------------------------
echo        Cai dat VBot Client (Socket)
echo ---------------------------------------------
echo.
call :connect
call :hide_packages
call :unhide_packages
echo.
echo Cho phep cai dat ung dung tu nguon khong xac dinh
adb shell settings put secure install_non_market_apps 1
set apk3=PhicommR1_VBotClient.apk
echo.
echo Dang tai phan mem PhicommR1_VBotClient.apk vao loa
adb push %apk3% /data/local/tmp/
echo.
echo Tai phan mem len loa thanh cong, bat dau cai dat phan mem
adb shell /system/bin/pm install -r /data/local/tmp/%apk3%
echo.
echo Cai dat thanh cong, dang xoa tep cai dat tam thoi...
adb shell rm /data/local/tmp/%apk3%
echo.
echo Dang khoi dong VBot Client (Socket)...
adb shell am start -n com.vbot_client.phicommr1/com.vbot_client.phicommr1.MainActivity
timeout /t 10 > nul
echo Nhan phim bat ky de tiep tuc!
pause > nul
cls
echo.
echo --------------------------
echo     CAI DAT HOAN TAT!
echo --------------------------
echo.
echo Nhan phim bat ky de khoi dong lai loa!
pause > nul
adb reboot > nul
echo.
echo Vui long doi loa khoi dong hoan tat!
echo.
echo Truy cap dia chi %ip%:8081 de vao bang cau hinh loa va trai nghiem!
echo.
echo Cam on ban da su dung!
echo.
echo Facebook: https://www.facebook.com/TWFyaW9uMDAx/
echo Group Facebook: https://www.facebook.com/groups/1148385343358824
echo Mail: VBot.Assistant@gmail.com
echo.
echo Cai dat xong, an phim bat ky de thoat!
pause > nul
goto menu

:install_apk
echo.
echo -------------------------------------------------------
echo        Setup Auto DLNA + Unisound (Fix am thanh)
echo -------------------------------------------------------
echo.
call :connect
echo.
echo Cho phep cai dat ung dung tu nguon khong xac dinh
adb shell settings put secure install_non_market_apps 1
set apk2=autodlna.apk
set apk1=unisound.apk
echo.
echo Dang tai phan mem vao loa
adb push %apk1% /data/local/tmp/
adb push %apk2% /data/local/tmp/
echo.
echo Tai phan mem len loa thanh cong, bat dau cai dat phan mem
adb shell /system/bin/pm install -r /data/local/tmp/%apk1%
adb shell /system/bin/pm install -r /data/local/tmp/%apk2%
echo.
echo Cai dat thanh cong, dang xoa tep cai dat tam thoi...
adb shell rm /data/local/tmp/%apk1%
adb shell rm /data/local/tmp/%apk2%
adb shell am startservice com.phicomm.speaker.player/.EchoService > nul 2>&1
adb shell am start com.phicomm.speaker.device/.ui.MainActivity > nul 2>&1
echo.
echo Dang khoi dong lai loa...
adb reboot > nul
echo.
echo Cai dat xong, an phim bat ky de thoat...
pause > nul
goto menu

:update
echo.
call :connect
if not exist ota\ota-%ver%.txt goto update_not
echo.
echo Xoa bo nho dem nang cap...
adb shell /system/bin/pm clear com.phicomm.speaker.otaservice > nul
echo.
echo Cho phep cai dat ung dung tu nguon khong xac dinh
adb shell settings put secure install_non_market_apps 1
set apk4=new_EchoService.apk
if not exist %apk4% goto file_not
echo.
echo Bat dau tai len %apk% (voi tu cach may chu nang cap)...
adb push %apk4% /data/local/tmp/
echo.
echo Tai len thanh cong, bat dau cai dat...
adb shell /system/bin/pm install -r /data/local/tmp/%apk4%
echo.
echo Cai dat thanh cong!
echo.
echo Xoa goi cai dat da tai len...
adb shell rm /data/local/tmp/%apk4%
echo.
echo Khoi dong tien trinh nang cap!
adb shell am startservice com.phicomm.speaker.player/.EchoService > nul 2>&1
echo.
echo Bat dau tai len cac tep nang cap...
adb push firmware\incremental-ota-%ver%.zip /sdcard/
echo.
echo Bat dau tai len cau hinh nang cap...
adb push ota\ota-%ver%.txt /sdcard/otaprop.txt
echo.
echo Tai len thanh cong!
echo.
echo Khoi dong tien trinh nang cap!
adb shell am startservice com.phicomm.speaker.player/.EchoService 
timeout /t 10 > nul
adb reboot > nul
echo.
echo Loa dang duoc khoi dong lai, vui long cho loa khoi dong lai xong!
echo Sau khi loa khoi dong lai xong vui long cau hinh lai mang Wifi cho loa!
echo Sau khi cau hinh lai Wifi cho loa xong vui long truy cap trang web http://r1.wxfsq.com:8080/?connect_ip=R1IP:8080 de hoan tat qua trinh nang cap!
pause > nul
goto menu

:update_not
echo.
echo Phien ban firmware %ver% cua ban dang la moi nhat, khong can nang cap firmware!
echo.
echo Nhan phim bat ky de quay lai!
pause > nul
goto menu

:clear_cache
echo.
call :connect
adb shell rm /sdcard/otaprop.txt > nul 2>&1
adb shell rm /sdcard/incremental*.zip > nul 2>&1
adb shell /system/bin/pm clear com.phicomm.speaker.otaservice > nul 2>&1
echo.
echo Bo nho dem da duoc xoa!
echo.
echo Nhan phim bat ky de quay lai!
pause > nul
goto menu

:file_not
echo.
echo Khong tim thay file new_EchoService.apk trong thu muc ung dung!
pause > nul
goto menu

:adbfile_not
color 04
echo.
echo Khong tim thay file adb, hay giai nen toan bo thu muc cai dat va chay lai file Update_R1 trong thu muc da giai nen!
pause > nul
exit