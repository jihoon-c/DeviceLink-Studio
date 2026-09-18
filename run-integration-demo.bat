@echo off
setlocal

set "PROJECT_DIRECTORY=%~dp0"
set "UNREAL_EDITOR=F:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
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

echo Starting the Unreal virtual device in game mode...
start "Unreal Virtual Device" "%UNREAL_EDITOR%" "%UPROJECT%" "/Game/Maps/DeviceLinkLab" -game -windowed -ResX=1280 -ResY=720

echo Building and starting the DeviceLink MFC console...
call "%PROJECT_DIRECTORY%run-mfc-host.bat"
exit /b %errorlevel%
