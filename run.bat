@echo off
setlocal EnableExtensions
cd /d "%~dp0"

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
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Dev-Run.ps1"
if errorlevel 1 (
    echo [ERROR] GraphVis could not be built or launched. Review the message above.
    pause
    exit /b 1
)
exit /b 0
