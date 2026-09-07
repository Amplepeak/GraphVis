@echo off
setlocal EnableExtensions
cd /d "%~dp0"

call "%~dp0install.bat" --no-pause
if not errorlevel 1 goto :build
set "RC=%ERRORLEVEL%"
goto :failed

:build
echo.
echo === Building the Windows standalone release ===
echo Creating the installer and self-contained runtime package...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Dev-Release.ps1"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" goto :failed

echo.
echo [OK] Standalone Windows package created under dist\
pause
exit /b 0

:failed
echo.
echo [ERROR] Windows release build failed. Review the message above.
echo See START_HERE.txt for the difference between this source package and an end-user release.
pause
exit /b %RC%
