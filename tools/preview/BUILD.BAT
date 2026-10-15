@echo off
rem build the PC preview of the radar screen with MSVC and run it
rem   BUILD.BAT [display] [seed]
rem display: ili9341 (default, the 320x240 CYD), st7796, ili9486, gc9a01, epd400, epd792, st7789
rem shots go to ..\..\shots, other displays to ..\..\shots\<display>
rem core\ sources are the same as in firmware
setlocal
set DISP=%~1
if "%DISP%"=="" set DISP=ili9341
set NUM=
if /i "%DISP%"=="ili9341" set NUM=1
if /i "%DISP%"=="st7796" set NUM=2
if /i "%DISP%"=="ili9486" set NUM=3
if /i "%DISP%"=="gc9a01" set NUM=4
if /i "%DISP%"=="epd400" set NUM=5
if /i "%DISP%"=="epd792" set NUM=6
if /i "%DISP%"=="st7789" set NUM=7
if "%NUM%"=="" (
  echo unknown display %DISP%, pick one of: ili9341 st7796 ili9486 gc9a01 epd400 epd792 st7789
  exit /b 1
)
set OUT=..\..\shots
if not "%NUM%"=="1" set OUT=..\..\shots\%DISP%

set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" (
  echo Visual Studio 2022 C++ tools not found: %VCVARS%
  exit /b 1
)
call "%VCVARS%" >nul
cd /d "%~dp0"
if not exist build\%DISP% mkdir build\%DISP%
cl /nologo /O2 /W3 /utf-8 /std:c11 /D_CRT_SECURE_NO_WARNINGS /DRADAR_DISPLAY=%NUM% ^
   /Fobuild\%DISP%\ /Febuild\%DISP%\preview.exe ^
   preview.c walk.c ..\..\core\map.c ..\..\core\store.c ..\..\core\app.c ..\..\core\gfx.c ..\..\core\font.c ^
   ..\..\core\ui.c ..\..\core\ui_radar.c ..\..\core\ui_views.c ..\..\core\ui_round.c ..\..\core\ui_paper.c ..\..\core\paper.c ^
   ..\..\core\font_small.c ..\..\core\font_medium.c ..\..\core\font_large.c ..\..\core\font_huge.c ^
   ..\..\core\font_paper_s.c ..\..\core\font_paper_m.c ..\..\core\font_paper_l.c ..\..\core\font_paper_xl.c ..\..\core\font_paper_xxl.c
if errorlevel 1 exit /b 1
if not exist %OUT% mkdir %OUT%
build\%DISP%\preview.exe %OUT% %2
