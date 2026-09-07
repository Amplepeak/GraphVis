@echo off
setlocal EnableExtensions
title GraphVis Repair
color 07
set "INSTALLER=%~dp0Install.exe"
if not exist "%INSTALLER%" set "INSTALLER=%~dp0..\Install.exe"
if not exist "%INSTALLER%" (
  color 0C
  echo [ERROR] Install.exe could not be found.
  echo Keep the Support folder beside Install.exe in the extracted GraphVis release.
  pause
  exit /b 2
)
echo Repairing GraphVis...
"%INSTALLER%" /REPAIR
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" goto :fail
echo.
echo [OK] GraphVis repair completed and the installed runtime was re-verified.
pause
exit /b 0
:fail
color 0C
echo.
echo [ERROR] GraphVis repair failed with exit code %RC%.
echo The installer window and this terminal remain visible for diagnostics.
pause
exit /b %RC%
