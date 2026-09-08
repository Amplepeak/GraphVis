@echo off
setlocal EnableExtensions
cd /d "%~dp0"

REM run.bat launches the staged build. It rebuilds only when there is nothing
REM staged, when a source file is newer than what is staged, or when you pass
REM --build. It used to build every time, with the fast preset, which meant a
REM finished release build sitting in build\stage was ignored and a second
REM build tree was configured instead.
REM
REM   run.bat            launch what is built (building only if out of date)
REM   run.bat --build    build first, then launch
REM   run.bat --full     build with the release preset, then launch

set PSARGS=
:parse
if "%~1"=="" goto :ready
if /i "%~1"=="--build" set PSARGS=%PSARGS% -Build
if /i "%~1"=="/build"  set PSARGS=%PSARGS% -Build
if /i "%~1"=="--full"  set PSARGS=%PSARGS% -Build -Full
if /i "%~1"=="/full"   set PSARGS=%PSARGS% -Build -Full
shift
goto :parse

:ready
if not exist ".tooling\dev-state.json" goto :setup
if not exist ".tooling\vcpkg\vcpkg.exe" goto :setup
goto :launch

:setup
call "%~dp0install.bat" --no-pause
if not errorlevel 1 goto :launch
echo [ERROR] Native developer setup failed. See START_HERE.txt for the next step.
pause
exit /b 1

:launch
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Dev-Run.ps1"%PSARGS%
if errorlevel 1 (
    echo [ERROR] GraphVis could not be launched. Review the message above.
    pause
    exit /b 1
)
exit /b 0
