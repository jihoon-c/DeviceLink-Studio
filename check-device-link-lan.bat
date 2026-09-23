@echo off
setlocal

set "DEVICELINK_PORT=5000"
set "DEVICELINK_HOST=%~1"

if not defined DEVICELINK_HOST (
    set /p "DEVICELINK_HOST=Desktop IPv4 address: "
)

if not defined DEVICELINK_HOST (
    echo [ERROR] An IPv4 address is required.
    pause
    exit /b 1
)

echo.
echo Checking %DEVICELINK_HOST%:%DEVICELINK_PORT% ...
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$result = Test-NetConnection -ComputerName $env:DEVICELINK_HOST -Port $env:DEVICELINK_PORT -InformationLevel Quiet -WarningAction SilentlyContinue; if ($result) { exit 0 } else { exit 1 }"

if errorlevel 1 (
    echo.
    echo [FAIL] TCP %DEVICELINK_PORT% is not reachable.
    echo Check that Unreal LAN mode is running, both PCs use the same network,
    echo and the desktop network profile and firewall rule are Private.
    pause
    exit /b 1
)

echo.
echo [OK] DeviceLink TCP %DEVICELINK_PORT% is reachable.
echo Start run-mfc-host.bat and enter %DEVICELINK_HOST% in the Host field.
pause
exit /b 0
