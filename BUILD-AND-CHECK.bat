@echo off
setlocal EnableExtensions
cd /d "%~dp0"
echo ================================================================
echo  GraphVis - build, then check, then draw every engine
echo ================================================================
echo.
echo One run, unattended. Start it and walk away: it builds the
echo release, runs the engine sweep and the property checks, and
echo writes a PNG of all 434 engines into gallery\.
echo.
echo Nothing here pauses until the very end, so it does not sit
echo waiting for a keypress halfway through. build.bat on its own
echo does pause, which is why this calls the release script
echo directly rather than calling build.bat.
echo.
echo Roughly: a few minutes to build, one minute to check, two to
echo draw. Everything lands in graph-check\ and gallery\.
echo.

set "STARTED=%TIME%"

REM ---------------------------------------------------------------- [1/3]
echo === [1/3] Toolchain ===
call "%~dp0install.bat" --no-pause
if errorlevel 1 (
  echo.
  echo [ERROR] Toolchain setup failed. Nothing else was run.
  pause
  exit /b 1
)

REM ---------------------------------------------------------------- [2/3]
echo.
echo === [2/3] Building the Windows standalone release ===
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Dev-Release.ps1"
set "BUILDRC=%ERRORLEVEL%"
if not "%BUILDRC%"=="0" (
  echo.
  echo [ERROR] Build failed with code %BUILDRC%.
  echo The checks below are skipped: running them against the previous
  echo binary would report on a build that no longer exists, which is
  echo worse than reporting nothing.
  pause
  exit /b %BUILDRC%
)
echo [OK] Build complete.

REM ---------------------------------------------------------------- [3/3]
REM
REM The checks are called rather than reimplemented, so there is exactly
REM one definition of what each of them does and this script cannot drift
REM from what CHECK-GRAPHS.bat and MAKE-GALLERY.bat actually run.
REM
REM Both of those end in `pause`. Piping a newline into them satisfies
REM that without editing either script, which keeps them usable on their
REM own and keeps this one unattended.
echo.
echo === [3/3] Checking, and drawing every engine ===
echo.
echo   ... engine sweep and property checks
echo. | call "%~dp0CHECK-GRAPHS.bat" >nul 2>&1
echo   ... gallery
echo. | call "%~dp0MAKE-GALLERY.bat" >nul 2>&1

echo.
echo ================================================================
echo  Done.  Started %STARTED%, finished %TIME%
echo ================================================================
echo.
if exist "%~dp0graph-check\summary.txt" (
  echo graph-check\summary.txt:
  type "%~dp0graph-check\summary.txt"
) else (
  echo [WARN] graph-check\summary.txt is missing - the sweep did not run.
)
echo.
echo Full output:
echo   graph-check\selftest-output.txt   the sweep, the checks, the clusters
echo   gallery\                          one PNG per engine
echo.
pause
exit /b 0
