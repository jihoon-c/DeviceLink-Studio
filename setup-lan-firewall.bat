@echo off
setlocal

set "RULE_NAME=DeviceLink Studio Unreal TCP 5000"

net session >nul 2>&1
if errorlevel 1 (
    echo Requesting administrator permission for the private-network firewall rule...
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b %errorlevel%
)

netsh advfirewall firewall show rule name="%RULE_NAME%" >nul 2>&1
if errorlevel 1 (
    netsh advfirewall firewall add rule name="%RULE_NAME%" dir=in action=allow protocol=TCP localport=5000 profile=private enable=yes
) else (
    netsh advfirewall firewall set rule name="%RULE_NAME%" new enable=yes profile=private action=allow protocol=TCP localport=5000
)

if errorlevel 1 (
    echo.
    echo [ERROR] Could not configure the Windows Firewall rule.
    pause
    exit /b 1
)

echo.
echo [OK] Windows Firewall is configured for DeviceLink TCP 5000.
echo Rule profile : Private only
echo Direction    : Inbound
echo Protocol     : TCP
echo Local port   : 5000
echo.
echo Do not change the network profile to Public for this test.
pause
exit /b 0
