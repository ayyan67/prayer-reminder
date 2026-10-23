@echo off
setlocal
rem Build PrayerReminder.exe with MinGW-w64 (MSYS2 UCRT64).
rem Nothing but the compiler is required; the app links only Windows DLLs.

set ROOT=%~dp0
set SRC=%ROOT%src
set OUT=%ROOT%PrayerReminder.exe

where gcc >nul 2>&1
if errorlevel 1 (
  echo gcc not found on PATH.
  echo Install MSYS2 and add C:\msys64\ucrt64\bin to PATH, or run build.sh.
  exit /b 1
)

echo Compiling resources...
windres "%SRC%\app.rc" -O coff -o "%ROOT%app.res" --include-dir "%SRC%"
if errorlevel 1 exit /b 1

echo Compiling...
gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -municode -mwindows ^
  -o "%OUT%" ^
  "%SRC%\main.c" "%SRC%\ui_main.c" "%SRC%\ui_toast.c" "%SRC%\gfx.c" ^
  "%SRC%\times.c" "%SRC%\sched.c" "%SRC%\config.c" "%SRC%\util.c" "%SRC%\net.c" ^
  "%ROOT%app.res" ^
  -lwinhttp -ldwmapi -lshell32 -lcomctl32 -lgdi32 -luser32 -lole32 -lwinmm -lm
if errorlevel 1 exit /b 1

del "%ROOT%app.res" >nul 2>&1
echo Built %OUT%
