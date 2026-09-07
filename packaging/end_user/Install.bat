@echo off
setlocal EnableExtensions
title GraphVis Installer
color 07
set "INSTALLER=%~dp0Install.exe"
if not exist "%INSTALLER%" goto :missing
start "GraphVis Setup" /wait "%INSTALLER%"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" goto :fail
exit /b 0
:missing
color 0C
echo [ERROR] Install.exe is missing from this folder.
echo Re-extract the official GraphVis Windows release ZIP and try again.
pause
exit /b 2
:fail
color 0C
echo.
echo [ERROR] GraphVis setup exited with code %RC%.
echo Please review the installer message above. Support tools are in the Support folder.
pause
exit /b %RC%
