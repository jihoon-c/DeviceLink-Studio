@echo off
setlocal

set "PROJECT_DIRECTORY=%~dp0"
set "SOAK_CYCLES=%~1"
set "SOAK_PORT=%~2"
if "%SOAK_CYCLES%"=="" set "SOAK_CYCLES=250"
if "%SOAK_PORT%"=="" set "SOAK_PORT=5000"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_DIRECTORY%run-unreal-integration-soak.ps1" -Cycles "%SOAK_CYCLES%" -Port "%SOAK_PORT%"
if errorlevel 1 goto :failed

pause
exit /b 0

:failed
echo [ERROR] Unreal integration soak failed.
pause
exit /b 1
