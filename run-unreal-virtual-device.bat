@echo off
setlocal

set "PROJECT_DIRECTORY=%~dp0"
set "UNREAL_EDITOR=F:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
set "UNREAL_BUILD=F:\UE_5.8\Engine\Build\BatchFiles\Build.bat"
set "UPROJECT=%PROJECT_DIRECTORY%UnrealVirtualDevice\UnrealVirtualDevice.uproject"

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

echo Starting Unreal Virtual Device...
start "Unreal Virtual Device" "%UNREAL_EDITOR%" "%UPROJECT%" "/Game/Maps/DeviceLinkLab" -game -windowed -ResX=1280 -ResY=720 -DeviceLinkAutoStart -DeviceLinkPort=5000 -ModelContextProtocolStartServer
exit /b 0
