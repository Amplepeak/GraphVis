@echo off
setlocal EnableExtensions
cd /d "%~dp0"
REM The passive audit in a window of its own. THIS IS THE ONE TO RUN.
REM
REM Same audit as PASSIVE-AUDIT.bat; the difference is the window. A console on
REM Windows 11 is hosted by Windows Terminal, which groups every console under
REM its own taskbar icon and ignores a shortcut's - so a console can never carry
REM the audit's icon, however the shortcut is made. A real window can, and it
REM can ask before it closes, which is the actual point.
REM
REM pythonw, so no console flashes up behind it.

REM ---- the shortcut, pointed at THIS file ---------------------------------
REM
REM It used to point at PASSIVE-AUDIT.bat, which is a console - so clicking it
REM opened the version that CANNOT show the icon, and no amount of work on the
REM icon itself could ever have fixed that. The target was the bug.
REM
REM CreateShortcut on a path that already exists loads it, so the target can be
REM compared and corrected; on a path that does not, it returns a blank one and
REM the same line creates it. The test sits inside the PowerShell command rather
REM than in a cmd if-block, because a caret-continued line inside an if-block is
REM fragile in cmd and produced no shortcut at all the first time this was tried.
set "GVDIR=%~dp0"
set "GVBAT=%~f0"
set "GVLNK=%~dp0GraphVis Passive Audit.lnk"
set "GVICO=%~dp0assets\branding\graphvis-audit.ico"
if not exist "%GVICO%" goto :haveshortcut
powershell -NoProfile -ExecutionPolicy Bypass -Command "$w=New-Object -ComObject WScript.Shell; $s=$w.CreateShortcut($env:GVLNK); if ($s.TargetPath -ne $env:GVBAT) { $s.TargetPath=$env:GVBAT; $s.WorkingDirectory=$env:GVDIR; $s.IconLocation=$env:GVICO; $s.Description='GraphVis passive audit - leave this running'; $s.Save(); Write-Output 'repointed' }" >"%TEMP%\gvlnk.txt" 2>&1
findstr /c:"repointed" "%TEMP%\gvlnk.txt" >nul 2>&1
if not errorlevel 1 echo Pointed the "GraphVis Passive Audit" shortcut at this window version.
del "%TEMP%\gvlnk.txt" >nul 2>&1
:haveshortcut

where pythonw >nul 2>&1
if errorlevel 1 goto :plainpython
start "" pythonw "%~dp0tools\audit_window.py"
exit /b 0

:plainpython
where python >nul 2>&1
if errorlevel 1 (
  echo [ERROR] python is not on PATH.
  pause
  exit /b 1
)
start "" python "%~dp0tools\audit_window.py"
exit /b 0
