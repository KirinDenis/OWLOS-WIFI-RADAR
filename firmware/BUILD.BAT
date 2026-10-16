@echo off
rem build OWLOS WiFi Radar firmware with ESP-IDF 5.5 (same install as for ViewOwl)
rem   BUILD.BAT                   build for the 320x240 CYD
rem   BUILD.BAT COM4              build, flash to COM4, open serial monitor (opening the port resets the board)
rem   BUILD.BAT st7796            build for another display
rem   BUILD.BAT st7796 COM4       same, and flash it
rem displays: ili9341 (2.8" CYD, all three revisions, the default)  st7796 (3.5" CYD)
rem           ili9486 (RPi LCD on OWLOSAirQuality)  gc9a01 (round 1.28", ESP32-C3)
rem           epd400 (4.2" e-paper, S3)  epd792 (5.79" e-paper, S3)
rem each display gets its own build folder and sdkconfig, so switching doesnt rebuild everything
setlocal
set DISP=%~1
set PORT=%~2
if /i "%DISP:~0,3%"=="COM" (
  set PORT=%DISP%
  set DISP=
)
if "%DISP%"=="" set DISP=ili9341
set NUM=
if /i "%DISP%"=="ili9341" (set NUM=1& set TARGET=esp32)
if /i "%DISP%"=="st7796"  (set NUM=2& set TARGET=esp32)
if /i "%DISP%"=="ili9486" (set NUM=3& set TARGET=esp32)
if /i "%DISP%"=="gc9a01"  (set NUM=4& set TARGET=esp32c3)
if /i "%DISP%"=="epd400"  (set NUM=5& set TARGET=esp32s3)
if /i "%DISP%"=="epd792"  (set NUM=6& set TARGET=esp32s3)
if "%NUM%"=="" (
  echo unknown display %DISP%, pick one of: ili9341 st7796 ili9486 gc9a01 epd400 epd792
  exit /b 1
)
rem the CYD keeps the plain build folder and sdkconfig it always had
set BDIR=build
set SDK=%~dp0sdkconfig
if not "%NUM%"=="1" (
  set BDIR=build_%DISP%
  set SDK=%~dp0build_%DISP%\sdkconfig
)

if "%IDF_PATH%"=="" set "IDF_PATH=%USERPROFILE%\esp\v5.5\esp-idf"
if "%IDF_TOOLS_PATH%"=="" set "IDF_TOOLS_PATH=%USERPROFILE%\.espressif"
if not exist "%IDF_PATH%\export.bat" (
  echo ESP-IDF not found at %IDF_PATH%
  exit /b 1
)
rem IDF venv was made with IDF's own python, not the system one, so put that first in PATH
for /d %%P in ("%IDF_TOOLS_PATH%\tools\idf-python\*") do set "PATH=%%P;%PATH%"
call "%IDF_PATH%\export.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
rem a build folder made where the project used to be (it was moved or copied) keeps the
rem old path in its CMake cache and CMake refuses it. such a folder is started fresh
set "HERE=%~dp0"
set "HERE=%HERE:\=/%"
if exist "%BDIR%\CMakeCache.txt" (
  findstr /i /c:"CMAKE_HOME_DIRECTORY:INTERNAL=%HERE:~0,-1%" "%BDIR%\CMakeCache.txt" >nul
  if errorlevel 1 (
    echo %BDIR% was made in another folder, starting it fresh
    rmdir /s /q "%BDIR%"
  )
)
set ARGS=-B %BDIR% "-DSDKCONFIG=%SDK%" -DIDF_TARGET=%TARGET% -DRADAR_DISPLAY=%NUM%
if "%PORT%"=="" (
  idf.py %ARGS% build
) else (
  idf.py %ARGS% -p %PORT% build flash monitor
)
