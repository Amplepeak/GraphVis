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
REM An optional gallery directory, so the pictures can be KEPT.
REM
REM The sweep draws all 434 engines and then throws the pictures away;
REM --selftest-gallery is the flag that saves them. Without this argument
REM BUILD-AND-CHECK.bat had to call this script and then MAKE-GALLERY.bat,
REM and MAKE-GALLERY runs exactly the same --selftest-plot with the flag
REM added - so every run rendered all 434 engines TWICE, once to check them
REM and once to look at them, for about a minute of duplicated work on every
REM build.
REM
REM Passing the directory here does both in one pass. MAKE-GALLERY.bat is
REM unchanged and still works on its own; this script with no argument
REM behaves exactly as it always did.
echo [1/3] Rendering every engine to a PDF...
if not "%~1"=="" (
  echo   ... and keeping every figure in "%~1"
  if exist "%~1" rmdir /s /q "%~1" 2>nul
  mkdir "%~1" 2>nul
  "%EXE%" --selftest-plot "%OUT%\selftest.pdf" --selftest-gallery "%~1" > "%OUT%\selftest-output.txt" 2>&1
) else (
  "%EXE%" --selftest-plot "%OUT%\selftest.pdf" > "%OUT%\selftest-output.txt" 2>&1
)
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
echo [2/3] Collecting the startup log and the data cache listing...
REM Qt puts AppLocalDataLocation at %LOCALAPPDATA%\<Org>\<App>, so the real
REM path is "GraphVis\GraphVis 18.4\18.4\logs" - one level deeper than the
REM obvious guess. The first version of this script tested the shallow path,
REM found the directory existed, copied nothing out of it and stopped looking.
REM Both are tried now, and neither is treated as the end of the search.
set FOUNDLOG=0
for %%D in (
  "%LOCALAPPDATA%\GraphVis\GraphVis 18.4\18.4\logs"
  "%LOCALAPPDATA%\GraphVis\18.4\logs"
  "%LOCALAPPDATA%\GraphVis\GraphVis 18.4\logs"
) do (
  if exist "%%~D\*.log" (
    copy /y "%%~D\*.log" "%OUT%\" >nul 2>&1
    echo   copied logs from %%~D
    echo logs from %%~D >> "%OUT%\summary.txt"
    set FOUNDLOG=1
  )
)
if "%FOUNDLOG%"=="0" (
  echo   no .log files found in any known location
  echo NO log files found >> "%OUT%\summary.txt"
)

REM What the importer actually produced. The file itself is not copied - it can
REM be hundreds of megabytes - only its name and size, which is enough to say
REM whether the conversion ran at all.
for %%D in (
  "%LOCALAPPDATA%\GraphVis\GraphVis 18.4\18.4\arrow-cache"
  "%LOCALAPPDATA%\GraphVis\18.4\arrow-cache"
) do (
  if exist "%%~D" (
    dir "%%~D" > "%OUT%\arrow-cache.txt" 2>&1
    echo   listed %%~D
  )
)

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
