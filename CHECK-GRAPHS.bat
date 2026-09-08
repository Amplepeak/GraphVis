@echo off
setlocal EnableExtensions
cd /d "%~dp0"
echo ================================================================
echo  GraphVis - why is the plot empty?
echo ================================================================
echo.
echo This runs the renderer on its own, with no interface and no data
echo of yours, and collects the logs. It writes everything into
echo graph-check\ in this folder. Nothing is sent anywhere.
echo.

set OUT=%~dp0graph-check
if exist "%OUT%" rmdir /s /q "%OUT%" 2>nul
mkdir "%OUT%" 2>nul

set EXE=%~dp0build\stage\graphvis.exe
if not exist "%EXE%" set EXE=%~dp0build\stage-fast\graphvis.exe
if not exist "%EXE%" (
  echo [ERROR] No built graphvis.exe found under build\stage or build\stage-fast.
  echo Run build.bat first.
  pause
  exit /b 1
)
echo Using %EXE%
echo Using %EXE% > "%OUT%\summary.txt"

echo.
echo [1/3] Rendering every engine to a PDF...
"%EXE%" --selftest-plot "%OUT%\selftest.pdf" > "%OUT%\selftest-output.txt" 2>&1
set RC=%ERRORLEVEL%
echo   exit code %RC%
echo selftest exit code %RC% >> "%OUT%\summary.txt"
if exist "%OUT%\selftest.pdf" (
  for %%A in ("%OUT%\selftest.pdf") do echo   selftest.pdf is %%~zA bytes & echo selftest.pdf bytes %%~zA >> "%OUT%\summary.txt"
) else (
  echo   selftest.pdf was NOT created
  echo selftest.pdf MISSING >> "%OUT%\summary.txt"
)

echo.
echo [2/3] Collecting the startup log...
set LOGDIR=%LOCALAPPDATA%\GraphVis\18.4\logs
if exist "%LOGDIR%" (
  copy /y "%LOGDIR%\*.log" "%OUT%\" >nul 2>&1
  echo   copied from %LOGDIR%
  echo startup logs from %LOGDIR% >> "%OUT%\summary.txt"
) else (
  set LOGDIR=%LOCALAPPDATA%\GraphVis\GraphVis 18.4\18.4\logs
  if exist "%LOGDIR%" (
    copy /y "%LOGDIR%\*.log" "%OUT%\" >nul 2>&1
    echo   copied from %LOGDIR%
    echo startup logs from %LOGDIR% >> "%OUT%\summary.txt"
  ) else (
    echo   no startup log directory found
    echo NO startup log directory >> "%OUT%\summary.txt"
  )
)

echo.
echo [3/3] Recording what is deployed beside the executable...
dir /b "%~dp0build\stage" > "%OUT%\stage-files.txt" 2>&1
dir /b "%~dp0build\stage\platforms" > "%OUT%\stage-platforms.txt" 2>&1
dir /b "%~dp0build\stage\qml" > "%OUT%\stage-qml.txt" 2>&1

echo.
echo ================================================================
echo  Done. Everything is in graph-check\
echo.
echo  Open graph-check\selftest.pdf and look at it:
echo    - real figures  = the renderer works, the problem is elsewhere
echo    - blank pages   = the renderer itself is the problem
echo ================================================================
echo.
pause
