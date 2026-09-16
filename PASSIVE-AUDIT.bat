@echo off
setlocal EnableExtensions
cd /d "%~dp0"

REM ---- so this window is not closed by accident ---------------------
REM
REM It is meant to be left running for hours, which makes it exactly the
REM window somebody tidies away without looking. A console cannot set its own
REM taskbar icon - that comes from whatever launched it - so:
REM
REM   title  names it on the taskbar and in Alt-Tab, which is what you read
REM          before clicking the X;
REM   color  gives it a blue ground no other GraphVis window has;
REM   a shortcut, made below, carries the icon. Launch THAT and the window
REM          gets the magnifier mark instead of the generic console one.
title GraphVis Passive Audit - leave running
color 1F

REM ---- the shortcut belongs to the WINDOW version, not to this one -------
REM
REM This file used to create a shortcut pointing at ITSELF. That was the bug
REM behind three rounds of icon work: this is a console, Windows 11 hosts
REM consoles in Windows Terminal, and Windows Terminal ignores a shortcut's
REM icon - so the shortcut could never show the audit's mark however the .ico
REM was built. The shortcut is made by PASSIVE-AUDIT-WINDOW.bat now and points
REM there, because a real window can carry an icon and this cannot.
REM
REM Running this file directly is still perfectly fine - it is the same audit.
set "GVLNK=%~dp0GraphVis Passive Audit.lnk"
set "GVWIN=%~dp0PASSIVE-AUDIT-WINDOW.bat"
if not exist "%GVLNK%" goto :haveshortcut
if not exist "%GVWIN%" goto :haveshortcut
powershell -NoProfile -ExecutionPolicy Bypass -Command "$w=New-Object -ComObject WScript.Shell; $s=$w.CreateShortcut($env:GVLNK); if ($s.TargetPath -ne $env:GVWIN) { $s.TargetPath=$env:GVWIN; $s.WorkingDirectory=(Split-Path $env:GVWIN); $s.Save() }" >nul 2>&1

:haveshortcut

REM ================================================================
REM  A standing audit. Leave it running.
REM ================================================================
REM
REM Looks through the source for what is dead, what is suspicious and what is
REM likely slow, and writes build-reports\AUDIT.md. Then waits and does it
REM again, so findings that arrive between sit-down audits are found by the
REM time anyone looks.
REM
REM STATIC AND READ-ONLY. It compiles nothing, renders nothing, opens no
REM window, and writes nothing outside build-reports\ - so it is safe to leave
REM running while you build, and a build is not slowed by it.
REM
REM It reports only what is NEW since the previous pass, so it stays readable
REM rather than becoming wallpaper. To start from a clean slate - after a
REM review, say:
REM
REM     python tools\passive_audit.py --accept
REM
REM marks everything currently found as known, and the next pass reports only
REM what appears after it.
REM
REM   PASSIVE-AUDIT.bat          watch: a pass whenever anything changes
REM   PASSIVE-AUDIT.bat 300      ...and at least every 5 minutes when idle
REM   PASSIVE-AUDIT.bat once     one pass, then stop
REM
REM IT WATCHES RATHER THAN TICKS.
REM
REM The first version ran a pass every fifteen minutes, which is either too
REM slow to be useful after a save or too frequent to be free. A sweep of the
REM tree's timestamps costs about 60 ms for three thousand files, so it asks
REM every few seconds instead and audits when something has actually changed -
REM a report within seconds of a save, and near-zero cost the rest of the time.
REM The idle number below is only a floor, so the report never goes stale.
set "IDLE=%~1"
if "%IDLE%"=="" set "IDLE=900"

where python >nul 2>&1
if errorlevel 1 (
  echo [ERROR] python is not on PATH, so the audit cannot run.
  pause
  exit /b 1
)

if /I "%IDLE%"=="once" (
  python "%~dp0tools\passive_audit.py" --print
  pause
  exit /b 0
)

echo  ================================================================
echo   GraphVis passive audit - LEAVE THIS RUNNING
echo  ================================================================
echo.
echo  A pass whenever a source file changes, and at least every %IDLE%s.
echo  Report: build-reports\AUDIT.md
echo  Nothing is built, rendered or deleted; safe to run during a build.
echo.
echo  Closing this window stops the audit and nothing else.
echo.
python "%~dp0tools\passive_audit.py" --watch --idle %IDLE%
echo.
echo  The audit stopped. Close this window, or run it again.
pause
