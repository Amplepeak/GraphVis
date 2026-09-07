@echo off
setlocal EnableExtensions
cd /d "%~dp0"
echo Building and verifying GraphVis. Output is also written to verify-log.txt
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Verify-Manifest.ps1" %* > "%~dp0verify-log.txt" 2>&1
type "%~dp0verify-log.txt"
echo.
echo ---- Done. Full output saved to verify-log.txt ----
pause
