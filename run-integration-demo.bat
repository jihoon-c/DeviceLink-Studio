@echo off
setlocal

set "PROJECT_DIRECTORY=%~dp0"
set "UNREAL_EDITOR=F:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
set "UNREAL_EDITOR_CMD=F:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
set "UNREAL_BUILD=F:\UE_5.8\Engine\Build\BatchFiles\Build.bat"
set "UPROJECT=%PROJECT_DIRECTORY%UnrealVirtualDevice\UnrealVirtualDevice.uproject"
set "CAMERA_CONFIG_SCRIPT=%PROJECT_DIRECTORY%UnrealVirtualDevice\SourceAssets\DeviceLink\configure_articulated_camera.py"

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

echo Building the Unreal virtual device...
call "%UNREAL_BUILD%" UnrealVirtualDeviceEditor Win64 Development -Project="%UPROJECT%" -WaitMutex -NoHotReloadFromIDE
if errorlevel 1 (
    echo [ERROR] Unreal build failed. Save and close any open Unreal Editor instance, then retry.
    pause
    exit /b 1
)

echo Applying the connected articulated camera assets...
"%UNREAL_EDITOR_CMD%" "%UPROJECT%" -unattended -nop4 -nosplash -nullrhi -ExecutePythonScript="%CAMERA_CONFIG_SCRIPT%"
if errorlevel 1 (
    echo [ERROR] Could not apply the connected camera assets.
    pause
    exit /b 1
)

echo Starting the Unreal virtual device in game mode...
start "Unreal Virtual Device" "%UNREAL_EDITOR%" "%UPROJECT%" "/Game/Maps/DeviceLinkLab" -game -windowed -ResX=1280 -ResY=720 -DeviceLinkAutoStart -DeviceLinkPort=5000

echo Building and starting the DeviceLink MFC console...
call "%PROJECT_DIRECTORY%run-mfc-host.bat"
exit /b %errorlevel%
