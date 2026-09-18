@echo off
setlocal

set "PROJECT_DIRECTORY=%~dp0"
set "CMAKE_EXECUTABLE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "VCVARS_SCRIPT=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "BUILD_DIRECTORY=%PROJECT_DIRECTORY%build-mfc-host"

if not exist "%CMAKE_EXECUTABLE%" (
    echo [ERROR] CMake was not found: %CMAKE_EXECUTABLE%
    pause
    exit /b 1
)

if not exist "%VCVARS_SCRIPT%" (
    echo [ERROR] Visual Studio C++ environment was not found: %VCVARS_SCRIPT%
    pause
    exit /b 1
)

call "%VCVARS_SCRIPT%" >nul
if errorlevel 1 goto :failed

pushd "%PROJECT_DIRECTORY%"
"%CMAKE_EXECUTABLE%" -G "NMake Makefiles" -S . -B "%BUILD_DIRECTORY%" -DDEVICELINK_BUILD_MFC_HOST=ON
if errorlevel 1 goto :failed_in_project

"%CMAKE_EXECUTABLE%" --build "%BUILD_DIRECTORY%"
if errorlevel 1 goto :failed_in_project

"%CMAKE_EXECUTABLE%" --build "%BUILD_DIRECTORY%" --target test
if errorlevel 1 goto :failed_in_project

echo.
echo [PASS] All automated tests passed. Starting the MFC host for visual inspection...
start "DeviceLink Studio" /D "%BUILD_DIRECTORY%" "%BUILD_DIRECTORY%\DeviceLinkMfcHost.exe"
popd
exit /b 0

:failed_in_project
popd

:failed
echo.
echo [ERROR] Validation failed. Review the messages above.
pause
exit /b 1
