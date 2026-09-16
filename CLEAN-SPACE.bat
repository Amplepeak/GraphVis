@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set ARGS=
if /i "%~1"=="/intermediates" set ARGS=-Intermediates
if /i "%~1"=="/buildtree"     set ARGS=-BuildTree
if /i "%~1"=="/all"           set ARGS=-All
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Clean-BuildCache.ps1" %ARGS%
echo.
pause
