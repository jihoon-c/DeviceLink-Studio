@echo off
setlocal

set "PROJECT_DIRECTORY=%~dp0"
set "RUNNER=%PROJECT_DIRECTORY%run-integration-demo.bat"

if not exist "%RUNNER%" (
    echo [ERROR] Self-test runner was not found: %RUNNER%
    pause
    exit /b 1
)

echo ============================================================
echo DeviceLink Studio - Single PC Self Test
echo ============================================================
echo Unreal virtual device and the MFC console will start locally.
echo Endpoint: 127.0.0.1:5000
echo ============================================================
echo.

call "%RUNNER%"
if errorlevel 1 (
    echo.
    echo [ERROR] Self-test startup failed.
    pause
    exit /b 1
)

exit /b 0
