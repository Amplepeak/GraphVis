@echo off
setlocal EnableExtensions
title GraphVis Uninstall
color 07
set "INSTALLDIR="
for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\GraphVis\18.4" /v InstallDir 2^>nul ^| find /i "InstallDir"') do set "INSTALLDIR=%%B"
if not defined INSTALLDIR set "INSTALLDIR=%LOCALAPPDATA%\Programs\GraphVis 18.4"
set "UNINSTALLER=%INSTALLDIR%\Uninstall.exe"
if not exist "%UNINSTALLER%" goto :missing
set "RELEASEDIR=%~dp0"
if exist "%RELEASEDIR%\Install.exe" goto :release_found
for %%I in ("%~dp0..") do set "RELEASEDIR=%%~fI"
:release_found
echo Uninstalling GraphVis from:
echo   %INSTALLDIR%
"%UNINSTALLER%"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" goto :fail
echo.
echo [OK] Installed files, settings, cache, shortcuts, and registry entries were removed.
if not exist "%RELEASEDIR%\Install.exe" goto :done
echo Removing the extracted GraphVis release folder. The original ZIP is not touched.
start "GraphVis cleanup" /b cmd.exe /d /c "timeout /t 2 /nobreak >nul ^& rd /s /q ""%RELEASEDIR%"""
:done
exit /b 0
:missing
color 0E
echo [WARNING] The registered GraphVis uninstaller is missing:
echo   %UNINSTALLER%
echo.
echo Run Repair.bat first to restore the uninstaller, then retry Uninstall.bat.
pause
exit /b 3
:fail
color 0C
echo.
echo [ERROR] GraphVis uninstaller returned exit code %RC%.
pause
exit /b %RC%
