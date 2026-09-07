@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "PAUSE_ON_EXIT=1"
if /i "%~1"=="--no-pause" set "PAUSE_ON_EXIT=0"

echo.
echo === GraphVis native developer setup ===
echo Installing or validating the CMake, Rust, and pinned vcpkg toolchain...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Dev-Bootstrap.ps1"
if errorlevel 1 goto :failed

echo.
echo [OK] Native developer environment is ready.
if "%PAUSE_ON_EXIT%"=="1" pause
exit /b 0

:failed
echo [ERROR] GraphVis native setup failed. Review the message above and run install.bat again.
echo.
echo This folder is GraphVis source code. For ordinary use, obtain a GraphVis Windows release ZIP containing Install.exe instead.
if "%PAUSE_ON_EXIT%"=="1" pause
exit /b 1
