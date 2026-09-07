@echo off
setlocal EnableExtensions
cd /d "%~dp0"
del /q "%~dp0full-log.txt" 2>nul
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Full-Diagnose.ps1" > "%~dp0full-log.txt" 2>&1
echo DONE >> "%~dp0full-log.txt"
exit /b 0
