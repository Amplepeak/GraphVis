@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title GraphVis build watcher
echo Leave this window open. It rebuilds GraphVis whenever a source file changes.
echo You can keep using your computer normally.
echo Close this window or press Ctrl+C to stop.
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Watch-Build.ps1"
pause
