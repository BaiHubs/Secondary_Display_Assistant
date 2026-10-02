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
REM core-ui 是 MSVC 预编译库，MinGW 侧用 gendef+dlltool 从 DLL 生成导入库后链接
set CFLAGS=-O2 -s -mwindows -fexec-charset=GBK -Icore-ui/include
set LIBS=-Lcore-ui/lib/mingw -lcore-ui -lcomctl32 -ldwmapi -lshcore -lgdi32 -luser32

set SRC=src\main.c src\wndproc.c src\hotkeys.c src\shellhook.c src\window_filter.c src\switcher.c src\tray.c src\config.c src\monitor.c src\move.c src\brightness.c src\display_cmd.c src\settings.c

echo [1/4] core-ui import library (gendef + dlltool) ...
gendef - core-ui\lib\dynamic\core-ui.dll > core-ui\lib\mingw\core-ui.def 2>nul
dlltool -d core-ui\lib\mingw\core-ui.def -D core-ui.dll -l core-ui\lib\mingw\libcore-ui.a

echo [2/4] winmover_res.o ...
windres res\winmover.rc -o winmover_res.o >nul 2>&1

echo [3/4] "%NAME%.exe" ...
%CC% %SRC% winmover_res.o -o "%NAME%.exe" %CFLAGS% %LIBS%
if exist "%NAME%.exe" (
    echo   OK
) else (
    echo   Retrying without resource object ...
    %CC% %SRC% -o "%NAME%.exe" %CFLAGS% %LIBS%
    if exist "%NAME%.exe" (echo   OK - no icon) else (echo   FAILED)
)

echo [4/4] runtime files (core-ui.dll + *.uix) ...
copy /y core-ui\lib\dynamic\core-ui.dll core-ui.dll >nul
copy /y src\settings.uix settings.uix >nul
copy /y src\switcher.uix switcher.uix >nul

echo.
if exist "%NAME%.exe" (echo   [OK] "%NAME%.exe") else (echo   [..] "%NAME%.exe")
echo.
echo Usage: "%NAME%.exe"             (tray background)
echo        "%NAME%.exe" --settings  (settings GUI)
echo.
pause
