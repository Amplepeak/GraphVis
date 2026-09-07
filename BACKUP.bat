@echo off
setlocal EnableExtensions
cd /d "%~dp0"
echo Backing up the GraphVis source to a dated zip.
echo Build output and downloaded packages are left out - build.bat regenerates them.
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Backup-Project.ps1" %*
echo.
pause
