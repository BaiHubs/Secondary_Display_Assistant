@echo off
cd /d %~dp0
setlocal enabledelayedexpansion

set CC=gcc
where gcc.exe >nul 2>&1
if errorlevel 1 (
    where x86_64-w64-mingw32-gcc.exe >nul 2>&1
    if errorlevel 1 (
        echo [Error] MinGW gcc not found.
        pause
        exit /b 1
    )
    set CC=x86_64-w64-mingw32-gcc
)
echo Compiler: %CC%

taskkill /f /im "Secondary Display Assistant.exe" >nul 2>&1

set NAME=Secondary Display Assistant
set CFLAGS=-O2 -s -mwindows -fexec-charset=GBK
set LIBS=-lcomctl32 -ldwmapi -lgdi32 -luser32

echo [1/3] winmover_res.o ...
windres winmover.rc -o winmover_res.o >nul 2>&1

echo [2/3] "%NAME%.exe" ...
%CC% winmover.c config.c monitor.c move.c brightness.c display_cmd.c gui.c settings.c winmover_res.o -o "%NAME%.exe" %CFLAGS% %LIBS%
if exist "%NAME%.exe" (
    echo   OK
) else (
    echo   Retrying without resource object ...
    %CC% winmover.c config.c monitor.c move.c brightness.c display_cmd.c gui.c settings.c -o "%NAME%.exe" %CFLAGS% %LIBS%
    if exist "%NAME%.exe" (echo   OK - no icon) else (echo   FAILED)
)

echo.
if exist "%NAME%.exe" (echo   [OK] "%NAME%.exe") else (echo   [..] "%NAME%.exe")
echo.
echo Usage: "%NAME%.exe"             (tray background)
echo        "%NAME%.exe" --settings  (settings GUI)
echo.
pause
