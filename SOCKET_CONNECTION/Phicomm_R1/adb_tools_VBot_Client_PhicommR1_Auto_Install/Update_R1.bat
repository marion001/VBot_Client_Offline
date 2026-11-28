@echo off
setlocal
title Cong Cu Cai Dat va Cap Nhat Loa Phicomm R1 - VBot Client (28/11/2026-2)
cd /d "%~dp0"
set "ADB=.\adb.exe"

:menu
cls
color 07
echo.
echo =======================================================
echo    CONG CU CAI DAT LOA PHICOMM R1 - VBOT CLIENT
echo    Phien ban goi APK: 28/11/2026-2
echo =======================================================
echo.
echo Vui long chon thao tac:
echo.
echo  1. Kiem tra thong tin va Firmware loa hien tai
echo  2. Cap nhat Firmware goc cho loa R1 (OTA)
echo  3. Xoa bo nho dem nang cap Firmware
echo  4. Cai dat Auto DLNA + Unisound (Fix am thanh he thong)
echo  5. Cai dat / Cap nhat VBot Client (Socket) + Cap quyen Nut nhan
echo  6. Xem Mat Khau dang nhap WebUI VBot Client da luu
echo  7. Xem Du Lieu Cau Hinh Config VBot Client (vbot_r1.xml)
echo  8. Cap lai quyen Nut nhan cung dinh loa (Button Accessibility)
echo  9. Exit (Thoat)
echo.
echo -------------------------------------------------------
set "choice="
set /p "choice= Chon thao tac ban muon thuc hien (1-9): "
if not defined choice goto invalid_choice
set "choice=%choice: =%"
if "%choice%"=="" goto invalid_choice
set "choice=%choice:~0,1%"
if /i "%choice%"=="1" goto check_fw
if /i "%choice%"=="2" goto update
if /i "%choice%"=="3" goto clear_cache
if /i "%choice%"=="4" goto install_apk
if /i "%choice%"=="5" goto install_ai
if /i "%choice%"=="6" goto view_pass_vbot_ui
if /i "%choice%"=="7" goto view_config_vbot
if /i "%choice%"=="8" goto grant_button_only
if /i "%choice%"=="9" goto exit
if /i "%choice%"=="0" goto exit

:invalid_choice
echo.
color 04
echo Lua chon khong hop le, vui long chon lai!
ping -n 2 127.0.0.1 > nul
goto menu

:connect
color 03
if not exist "%ADB%" goto adbfile_not
set "ip="
echo.
set /p "ip=Vui long nhap dia chi IP cua loa (VD: 192.168.1.50): "
if not defined ip (
    color 04
    echo Dia chi IP khong duoc de trong!
    ping -n 2 127.0.0.1 > nul
    goto connect
)
set "ip=%ip: =%"
if "%ip%"=="" (
    color 04
    echo Dia chi IP khong duoc de trong!
    ping -n 2 127.0.0.1 > nul
    goto connect
)
set "raw_ip="
for /f "tokens=1 delims=:" %%a in ("%ip%") do set "raw_ip=%%a"
if not defined raw_ip (
    color 04
    echo Dia chi IP khong hop le!
    ping -n 2 127.0.0.1 > nul
    goto connect
)
set "adb_target=%raw_ip%:5555"

:connect_ip
color 06
echo.
echo Bat dau ket noi toi loa tai %adb_target% ...
"%ADB%" connect %adb_target% < nul
set "adb_state="
for /f "tokens=1 delims= " %%i in ('.\adb.exe -s %adb_target% get-state 2^>nul ^< nul') do (
    for /f "tokens=1 delims= " %%x in ("%%i") do set "adb_state=%%x"
)
if /i "%adb_state%"=="device" goto test_connect

echo Dang khoi dong lai dich vu ADB va thu ket noi lai...
"%ADB%" disconnect %adb_target% > nul 2>&1 < nul
"%ADB%" kill-server > nul 2>&1 < nul
"%ADB%" connect %adb_target% < nul
ping -n 2 127.0.0.1 > nul
set "adb_state="
for /f "tokens=1 delims= " %%i in ('.\adb.exe -s %adb_target% get-state 2^>nul ^< nul') do (
    for /f "tokens=1 delims= " %%x in ("%%i") do set "adb_state=%%x"
)
if /i "%adb_state%"=="device" goto test_connect

color 04
echo.
echo Ket noi that bai toi %adb_target%!
echo Vui long kiem tra lai dia chi IP cua loa va dam bao PC cung mang LAN.
ping -n 3 127.0.0.1 > nul
goto connect

:run_error
color 04
echo Da xay ra loi giao tiep ADB, hay nhan phim bat ky de thu ket noi lai!
pause > nul
goto connect

:test_connect
color 06
echo Dang kiem tra phan hoi tu thiet bi, vui long cho...
"%ADB%" -s %adb_target% shell ls /system > nul 2>&1
if errorlevel 1 goto run_error
color 03
echo Ket noi thanh cong voi loa (%adb_target%)!
goto :eof

:device_info
echo.
set "serialno="
set "ver="
set "ipaddress="
set "build_host="
set "build_model="
set "hostname="
set "r1="

for /f "delims=" %%i in ('.\adb.exe -s %adb_target% shell getprop ro.serialno 2^>nul') do (
    for /f "tokens=* delims= " %%x in ("%%i") do set "serialno=%%x"
)
for /f "delims=" %%i in ('.\adb.exe -s %adb_target% shell getprop ro.build.version.incremental 2^>nul') do (
    for /f "tokens=1 delims= " %%x in ("%%i") do set "ver=%%x"
)
for /f "delims=" %%i in ('.\adb.exe -s %adb_target% shell getprop dhcp.wlan0.ipaddress 2^>nul') do (
    for /f "tokens=1 delims= " %%x in ("%%i") do set "ipaddress=%%x"
)
for /f "delims=" %%i in ('.\adb.exe -s %adb_target% shell getprop ro.build.host 2^>nul') do (
    for /f "tokens=* delims= " %%x in ("%%i") do set "build_host=%%x"
)
for /f "delims=" %%i in ('.\adb.exe -s %adb_target% shell getprop ro.product.model 2^>nul') do (
    for /f "tokens=* delims= " %%x in ("%%i") do set "build_model=%%x"
)
for /f "delims=" %%i in ('.\adb.exe -s %adb_target% shell getprop net.hostname 2^>nul') do (
    for /f "tokens=* delims= " %%x in ("%%i") do set "hostname=%%x"
)

if /i "%build_host%"=="phicomm" if /i "%build_model%"=="rk322x-box" set "r1=true"

color 03
if not "%hostname%"=="" echo Device name     : %hostname%
if not "%ver%"=="" echo Firmware version: %ver%
if not "%ipaddress%"=="" (
    echo Device IP       : %ipaddress%
) else (
    echo Device IP       : %raw_ip%
)
if not "%serialno%"=="" echo Device SN       : %serialno%
if not "%ver%"=="" (
    if %ver% gtr 2999 if %ver% neq 3448 (
        if "%r1%"=="true" (
            color 06
            echo.
            echo Luu y: Phien ban firmware R1 ^(%ver%^) khong phai ban moi nhat ^(3448^).
            echo Ban co the nang cap Firmware bang muc 2 trong cong cu nay!
        )
    )
)
goto :eof

:exit
echo.
echo Nhan phim bat ky de thoat!
pause > nul
exit /b 0

:check_fw
echo.
echo --------------------------------------------
echo        Kiem tra Firmware loa hien tai
echo --------------------------------------------
call :connect
call :device_info
echo.
echo Nhan phim bat ky de quay lai menu!
pause > nul
goto menu

:hide_packages
echo.
echo Tat cac ung dung goc gay xung dot tren loa:
"%ADB%" -s %adb_target% shell /system/bin/pm hide com.phicomm.speaker.device
"%ADB%" -s %adb_target% shell /system/bin/pm hide com.phicomm.speaker.otaservice
"%ADB%" -s %adb_target% shell /system/bin/pm hide com.phicomm.speaker.productiontest
"%ADB%" -s %adb_target% shell /system/bin/pm hide com.phicomm.speaker.bugreport
"%ADB%" -s %adb_target% shell am force-stop com.phicomm.speaker.device > nul 2>&1
"%ADB%" -s %adb_target% shell am force-stop com.phicomm.speaker.otaservice > nul 2>&1
goto :eof

:unhide_packages
echo.
echo Dam bao cac dich vu mang va he thong can thiet duoc bat:
"%ADB%" -s %adb_target% shell /system/bin/pm unhide com.phicomm.speaker.airskill
"%ADB%" -s %adb_target% shell /system/bin/pm unhide com.phicomm.speaker.exceptionreporter
"%ADB%" -s %adb_target% shell /system/bin/pm unhide com.phicomm.speaker.systemtool
"%ADB%" -s %adb_target% shell /system/bin/pm unhide com.phicomm.speaker.ijetty
"%ADB%" -s %adb_target% shell /system/bin/pm unhide com.phicomm.speaker.netctl
goto :eof

:grant_vbot_permissions
echo.
echo Dang cap quyen he thong va kich hoat Nut nhan cung dinh loa...
"%ADB%" -s %adb_target% shell pm grant com.vbot_client.phicommr1 android.permission.WRITE_SECURE_SETTINGS > nul 2>&1
"%ADB%" -s %adb_target% shell settings put secure enabled_accessibility_services com.vbot_client.phicommr1/com.vbot_client.phicommr1.VBotKeyAccessibilityService
"%ADB%" -s %adb_target% shell settings put secure accessibility_enabled 1
goto :eof

:install_ai
echo.
echo -------------------------------------------------------
echo    Cai dat / Cap nhat VBot Client (Socket) cho R1
echo -------------------------------------------------------
set "apk3=PhicommR1_VBotClient.apk"
if not exist "%apk3%" (
    color 04
    echo.
    echo Khong tim thay file %apk3% trong thu muc hien tai!
    echo Vui long kiem tra lai thu muc giai nen va thu lai.
    pause > nul
    goto menu
)
call :connect
call :hide_packages
call :unhide_packages
echo.
echo Cho phep cai dat ung dung tu nguon khong xac dinh...
"%ADB%" -s %adb_target% shell settings put secure install_non_market_apps 1
echo.
echo Dang tai phan mem %apk3% len loa (%adb_target%)...
"%ADB%" -s %adb_target% push "%apk3%" /data/local/tmp/vbot.apk
if errorlevel 1 (
    color 04
    echo.
    echo Loi khi day file %apk3% len loa! Vui long kiem tra ket noi mang va thu lai.
    pause > nul
    goto menu
)
echo.
echo Tai len thanh cong, bat dau cai dat VBot Client...
set "install_ok="
for /f "delims=" %%i in ('.\adb.exe -s %adb_target% shell /system/bin/pm install -r /data/local/tmp/vbot.apk 2^>^&1') do (
    echo %%i
    echo %%i | findstr /i "Success" > nul && set "install_ok=1"
)
echo.
echo Dang xoa tep cai dat tam thoi...
"%ADB%" -s %adb_target% shell rm /data/local/tmp/vbot.apk > nul 2>&1

if not "%install_ok%"=="1" (
    color 04
    echo.
    echo CAI DAT THAT BAI! Vui long xem thong bao loi phia tren ^(kiem tra dung luong trong cua loa^).
    pause > nul
    goto menu
)

call :grant_vbot_permissions

echo.
echo Dang khoi dong VBot Client...
"%ADB%" -s %adb_target% shell am start -n com.vbot_client.phicommr1/.MainActivity < nul
ping -n 4 127.0.0.1 > nul
cls
color 02
echo.
echo =======================================================
echo             CAI DAT VBOT CLIENT HOAN TAT!
echo =======================================================
echo.
echo Giao dien quan tri WebUI cua loa: http://%raw_ip%:8081
echo.
set "do_reboot="
set /p "do_reboot=Ban co muon khoi dong lai (Reboot) loa ngay bay gio khong? (Khuyen dung khi cai lan dau) [Y/N]: "
if /i "%do_reboot%"=="Y" (
    echo.
    echo Dang gui lenh khoi dong lai loa...
    "%ADB%" -s %adb_target% reboot > nul 2>&1
    echo Vui long doi loa khoi dong hoan tat ^(~45 giay^), sau do truy cap:
    echo   http://%raw_ip%:8081
) else (
    echo.
    echo Ung dung VBot Client da duoc khoi chay! Ban co the truy cap ngay:
    echo   http://%raw_ip%:8081
)
echo.
echo Cam on ban da su dung!
echo Facebook      : https://www.facebook.com/TWFyaW9uMDAx/
echo Group Facebook: https://www.facebook.com/groups/1148385343358824
echo Mail          : VBot.Assistant@gmail.com
echo.
echo Nhan phim bat ky de quay lai menu!
pause > nul
goto menu

:grant_button_only
echo.
echo -------------------------------------------------------
echo    Cap quyen Nut nhan cung dinh loa (Accessibility)
echo -------------------------------------------------------
call :connect
call :grant_vbot_permissions
echo.
color 02
echo Da cap quyen WRITE_SECURE_SETTINGS va bat VBotKeyAccessibilityService thanh cong!
echo Nhan phim bat ky de quay lai menu!
pause > nul
goto menu

:install_apk
echo.
echo -------------------------------------------------------
echo        Setup Auto DLNA + Unisound (Fix am thanh)
echo -------------------------------------------------------
set "apk1=unisound.apk"
set "apk2=autodlna.apk"
if not exist "%apk1%" (
    color 04
    echo Khong tim thay file %apk1% trong thu muc hien tai!
    pause > nul
    goto menu
)
if not exist "%apk2%" (
    color 04
    echo Khong tim thay file %apk2% trong thu muc hien tai!
    pause > nul
    goto menu
)
call :connect
echo.
echo Cho phep cai dat ung dung tu nguon khong xac dinh...
"%ADB%" -s %adb_target% shell settings put secure install_non_market_apps 1
echo.
echo Dang tai %apk1% va %apk2% len loa...
"%ADB%" -s %adb_target% push "%apk1%" /data/local/tmp/%apk1%
if errorlevel 1 (
    color 04
    echo Loi khi tai %apk1% len loa!
    pause > nul
    goto menu
)
"%ADB%" -s %adb_target% push "%apk2%" /data/local/tmp/%apk2%
if errorlevel 1 (
    color 04
    echo Loi khi tai %apk2% len loa!
    pause > nul
    goto menu
)
echo.
echo Tai len thanh cong, bat dau cai dat phan mem...
"%ADB%" -s %adb_target% shell /system/bin/pm install -r /data/local/tmp/%apk1%
"%ADB%" -s %adb_target% shell /system/bin/pm install -r /data/local/tmp/%apk2%
echo.
echo Dang xoa tep cai dat tam thoi...
"%ADB%" -s %adb_target% shell rm /data/local/tmp/%apk1% > nul 2>&1
"%ADB%" -s %adb_target% shell rm /data/local/tmp/%apk2% > nul 2>&1
"%ADB%" -s %adb_target% shell am startservice com.phicomm.speaker.player/.EchoService > nul 2>&1
"%ADB%" -s %adb_target% shell am start -n com.phicomm.speaker.device/.ui.MainActivity > nul 2>&1
echo.
echo Dang khoi dong lai loa...
"%ADB%" -s %adb_target% reboot > nul 2>&1
echo.
echo Cai dat xong, nhan phim bat ky de quay lai menu...
pause > nul
goto menu

:view_pass_vbot_ui
echo.
echo -------------------------------------------------------
echo     Xem Thong Tin Dang Nhap WebUI VBot Client R1
echo -------------------------------------------------------
call :connect
echo.
echo Thong tin xac thuc giao dien WebUI (http://%raw_ip%:8081):
echo -------------------------------------------------------
set "found_auth="
for /f "delims=" %%i in ('.\adb.exe -s %adb_target% shell run-as com.vbot_client.phicommr1 cat shared_prefs/vbot_r1.xml 2^>nul ^| findstr /i "webUiAuthEnabled webUiUsername webUiPassword"') do (
    echo   %%i
    set "found_auth=1"
)
if not "%found_auth%"=="1" (
    echo   Chua co mat khau WebUI nao duoc luu ^(hoac WebUI dang tat xac thuc dang nhap^).
)
echo -------------------------------------------------------
echo.
echo Nhan phim bat ky de quay lai menu!
pause > nul
goto menu

:view_config_vbot
echo.
echo -------------------------------------------------------
echo     Xem Du Lieu Config VBot Client Phicomm R1
echo -------------------------------------------------------
call :connect
echo.
echo Du lieu cau hinh tai shared_prefs/vbot_r1.xml:
echo -------------------------------------------------------
"%ADB%" -s %adb_target% shell run-as com.vbot_client.phicommr1 cat shared_prefs/vbot_r1.xml
echo -------------------------------------------------------
echo.
echo Nhan phim bat ky de quay lai menu!
pause > nul
goto menu

:update
echo.
echo -------------------------------------------------------
echo          Cap nhat Firmware goc cho loa R1
echo -------------------------------------------------------
call :connect
call :device_info
if "%ver%"=="" (
    color 04
    echo Khong doc duoc phien ban Firmware hien tai cua loa!
    pause > nul
    goto menu
)
if not exist "ota\ota-%ver%.txt" goto update_not
if not exist "firmware\incremental-ota-%ver%.zip" (
    color 04
    echo.
    echo Khong tim thay goi firmware\incremental-ota-%ver%.zip trong thu muc ung dung!
    pause > nul
    goto menu
)
set "apk4=new_EchoService.apk"
if not exist "%apk4%" goto file_not

echo.
echo Xoa bo nho dem nang cap cu...
"%ADB%" -s %adb_target% shell /system/bin/pm clear com.phicomm.speaker.otaservice > nul 2>&1
echo.
echo Cho phep cai dat ung dung tu nguon khong xac dinh...
"%ADB%" -s %adb_target% shell settings put secure install_non_market_apps 1
echo.
echo Bat dau tai len %apk4% (dich vu ho tro nang cap)...
"%ADB%" -s %adb_target% push "%apk4%" /data/local/tmp/%apk4%
if errorlevel 1 (
    color 04
    echo Loi khi tai len %apk4%!
    pause > nul
    goto menu
)
echo.
echo Tai len thanh cong, bat dau cai dat %apk4%...
"%ADB%" -s %adb_target% shell /system/bin/pm install -r /data/local/tmp/%apk4%
echo.
echo Xoa goi cai dat tam thoi...
"%ADB%" -s %adb_target% shell rm /data/local/tmp/%apk4% > nul 2>&1
echo.
echo Khoi dong dich vu EchoService...
"%ADB%" -s %adb_target% shell am startservice com.phicomm.speaker.player/.EchoService > nul 2>&1
echo.
echo Bat dau tai len goi nang cap firmware\incremental-ota-%ver%.zip ...
"%ADB%" -s %adb_target% push "firmware\incremental-ota-%ver%.zip" /sdcard/
if errorlevel 1 (
    color 04
    echo Loi khi tai goi firmware len /sdcard/!
    pause > nul
    goto menu
)
echo.
echo Bat dau tai len cau hinh nang cap ota\ota-%ver%.txt ...
"%ADB%" -s %adb_target% push "ota\ota-%ver%.txt" /sdcard/otaprop.txt
echo.
echo Tai len thanh cong! Khoi dong tien trinh nang cap...
"%ADB%" -s %adb_target% shell am startservice com.phicomm.speaker.player/.EchoService < nul
ping -n 10 127.0.0.1 > nul
"%ADB%" -s %adb_target% reboot > nul 2>&1 < nul
echo.
echo Loa dang duoc khoi dong lai, vui long cho loa khoi dong xong!
echo Sau khi loa khoi dong lai xong, neu can cau hinh Wifi hay ket noi lai Wifi cho loa,
echo sau do truy cap dia chi sau de hoan tat qua trinh nang cap:
echo   http://r1.wxfsq.com:8080/?connect_ip=%raw_ip%:8080
echo.
pause > nul
goto menu

:update_not
echo.
color 02
echo Phien ban firmware %ver% cua ban dang la moi nhat (hoac khong can goi OTA trung gian)!
echo.
echo Nhan phim bat ky de quay lai menu!
pause > nul
goto menu

:clear_cache
echo.
call :connect
"%ADB%" -s %adb_target% shell rm /sdcard/otaprop.txt > nul 2>&1
"%ADB%" -s %adb_target% shell rm /sdcard/incremental*.zip > nul 2>&1
"%ADB%" -s %adb_target% shell /system/bin/pm clear com.phicomm.speaker.otaservice > nul 2>&1
echo.
color 02
echo Bo nho dem nang cap tren loa da duoc xoa!
echo.
echo Nhan phim bat ky de quay lai menu!
pause > nul
goto menu

:file_not
color 04
echo.
echo Khong tim thay file new_EchoService.apk trong thu muc ung dung!
pause > nul
goto menu

:adbfile_not
color 04
echo.
echo Khong tim thay file adb.exe!
echo Hay giai nen toan bo thu muc cai dat va chay lai file Update_R1.bat trong thu muc da giai nen!
pause > nul
exit /b 1
