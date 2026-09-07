@echo off
setlocal EnableExtensions
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Collect-CrashReport.ps1"
echo.
echo Written to crash-report.txt - tell Claude and it will read it.
pause
