@echo off
setlocal EnableExtensions
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Diagnose-Run.ps1" %* > "%~dp0diagnose-log.txt" 2>&1
type "%~dp0diagnose-log.txt"
echo.
echo ---- Done. Saved to diagnose-log.txt ----
pause
