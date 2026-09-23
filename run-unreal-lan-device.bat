@echo off
setlocal

set "PROJECT_DIRECTORY=%~dp0"
set "UNREAL_EDITOR=F:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
set "UNREAL_BUILD=F:\UE_5.8\Engine\Build\BatchFiles\Build.bat"
set "UPROJECT=%PROJECT_DIRECTORY%UnrealVirtualDevice\UnrealVirtualDevice.uproject"
set "DEVICELINK_PORT=5000"

if not exist "%UNREAL_EDITOR%" (
    echo [ERROR] Unreal Editor was not found: %UNREAL_EDITOR%
    pause
    exit /b 1
)

if not exist "%UPROJECT%" (
    echo [ERROR] Unreal project was not found: %UPROJECT%
    pause
    exit /b 1
)

echo Building Unreal Virtual Device...
call "%UNREAL_BUILD%" UnrealVirtualDeviceEditor Win64 Development -Project="%UPROJECT%" -WaitMutex -NoHotReloadFromIDE
if errorlevel 1 (
    echo [ERROR] Build failed. Save and close any open Unreal Editor instance, then retry.
    pause
    exit /b 1
)

echo ============================================================
echo DeviceLink LAN virtual device
echo ============================================================
echo Bind address : 0.0.0.0
echo TCP port     : %DEVICELINK_PORT%
echo.
echo Enter one of this desktop's IPv4 addresses in the laptop MFC Host field:
ipconfig | findstr /i "IPv4"
echo.
echo Use only on a trusted private network.
echo If this is the first LAN test, run setup-lan-firewall.bat as administrator.
echo ============================================================
echo.

start "Unreal Virtual Device - LAN" "%UNREAL_EDITOR%" "%UPROJECT%" "/Game/Maps/DeviceLinkLab" -game -windowed -ResX=1280 -ResY=720 -DeviceLinkAutoStart -DeviceLinkAllowLan -DeviceLinkPort=%DEVICELINK_PORT%
if errorlevel 1 (
    echo [ERROR] Failed to start Unreal Virtual Device.
    pause
    exit /b 1
)

pause
exit /b 0
