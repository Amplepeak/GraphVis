@echo off
setlocal EnableExtensions
cd /d "%~dp0"
REM The interface tests: real QML components, instantiated and asserted.
REM
REM Everything else in this project drives the backend and never presses
REM anything. These instantiate the actual panels with stand-in data, so a
REM binding that assigns undefined to a bool, or a delegate reading a property
REM it was never given, fails here instead of in front of you.
REM
REM BUILD-AND-CHECK.bat runs these too. This is for running them alone.
where python >nul 2>&1
if errorlevel 1 (
  echo [ERROR] python is not on PATH.
  pause
  exit /b 1
)
python "%~dp0tools\run_ui_tests.py" %*
echo.
pause
