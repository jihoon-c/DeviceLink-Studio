@echo off
setlocal

set "PROJECT_DIRECTORY=%~dp0"
set "CMAKE_EXECUTABLE="
set "NINJA_EXECUTABLE="
set "VCVARS_SCRIPT="
set "BUILD_DIRECTORY=%PROJECT_DIRECTORY%build-mfc-host-ninja"

for %%V in (18 2022) do for %%E in (Community Professional Enterprise BuildTools) do (
    if not defined CMAKE_EXECUTABLE if exist "%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXECUTABLE=%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if not defined NINJA_EXECUTABLE if exist "%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "NINJA_EXECUTABLE=%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    if not defined VCVARS_SCRIPT if exist "%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS_SCRIPT=%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat"
)

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

if not exist "%NINJA_EXECUTABLE%" (
    echo [ERROR] Ninja was not found: %NINJA_EXECUTABLE%
    pause
    exit /b 1
)

call "%VCVARS_SCRIPT%" >nul
if errorlevel 1 (
    echo [ERROR] Failed to initialize the Visual Studio C++ environment.
    pause
    exit /b 1
)

pushd "%PROJECT_DIRECTORY%"
"%CMAKE_EXECUTABLE%" -G Ninja -S . -B "%BUILD_DIRECTORY%" -DDEVICELINK_BUILD_MFC_HOST=ON -DCMAKE_MAKE_PROGRAM="%NINJA_EXECUTABLE%"
if errorlevel 1 goto :build_failed

"%CMAKE_EXECUTABLE%" --build "%BUILD_DIRECTORY%" --target DeviceLinkMfcHost
if errorlevel 1 goto :build_failed

echo.
echo Starting DeviceLink Studio...
start "DeviceLink Studio" /D "%BUILD_DIRECTORY%" "%BUILD_DIRECTORY%\DeviceLinkMfcHost.exe"
popd
exit /b 0

:build_failed
popd
echo.
echo [ERROR] Build failed. Review the messages above.
pause
exit /b 1
