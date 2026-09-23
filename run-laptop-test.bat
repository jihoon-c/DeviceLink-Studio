@echo off
setlocal

set "PROJECT_DIRECTORY=%~dp0"

:menu
cls
echo ============================================================
echo DeviceLink Studio - Two PC LAN Test
echo ============================================================
echo [1] Desktop - start Unreal virtual device server
echo [2] Laptop  - check server and start MFC console
echo [3] Exit
echo ============================================================
choice /c 123 /n /m "Select this PC's role [1-3]: "

if errorlevel 3 exit /b 0
if errorlevel 2 goto laptop
if errorlevel 1 goto desktop

:desktop
echo.
echo Configuring the TCP 5000 Private-network firewall rule...
call "%PROJECT_DIRECTORY%setup-lan-firewall.bat"
if errorlevel 1 goto failed

call "%PROJECT_DIRECTORY%run-unreal-lan-device.bat"
if errorlevel 1 goto failed
exit /b 0

:laptop
echo.
set "DEVICELINK_HOST="
set /p "DEVICELINK_HOST=Desktop IPv4 address: "
if not defined DEVICELINK_HOST (
    echo [ERROR] An IPv4 address is required.
    pause
    goto menu
)

call "%PROJECT_DIRECTORY%check-device-link-lan.bat" "%DEVICELINK_HOST%"
if errorlevel 1 goto failed

call "%PROJECT_DIRECTORY%run-mfc-host.bat"
if errorlevel 1 goto failed
exit /b 0

:failed
echo.
echo [ERROR] DeviceLink LAN test startup failed.
pause
exit /b 1
