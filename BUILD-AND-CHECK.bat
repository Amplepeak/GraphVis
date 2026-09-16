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
REM ONE RENDER PASS, NOT TWO.
REM
REM This used to call CHECK-GRAPHS.bat and then MAKE-GALLERY.bat. Both run
REM `graphvis.exe --selftest-plot`, and --selftest-gallery only decides
REM whether the sweep KEEPS the pictures it has already drawn - so the second
REM call redid the whole 434-engine sweep, the regression checks and the
REM property checks from scratch in order to save the PNGs. About a minute of
REM the "one minute to check, two to draw" above was the same work done twice.
REM
REM CHECK-GRAPHS.bat now takes the gallery directory as an argument and does
REM both in one pass. MAKE-GALLERY.bat is untouched and still works on its own
REM for when only the pictures are wanted.
REM The clock for the checking phase, so the report can say whether the
REM checks are getting slower. Stamped rather than subtracted in batch,
REM where two %TIME% values either side of midnight do not behave.
if exist "%~dp0tools\build_report.py" (
  where python >nul 2>&1 && python "%~dp0tools\build_report.py" --mark-start >nul 2>&1
)
echo   ... engine sweep, property checks, and the gallery
echo. | call "%~dp0CHECK-GRAPHS.bat" "%~dp0gallery" >nul 2>&1

REM ---- the interface, which nothing else here opens -----------------
REM
REM Every check above drives the backend directly: the sweep draws 434
REM engines and the property checks measure them, and not one of them
REM ever creates a window. A QML binding is only evaluated when one
REM does, so a name that resolves to nothing - a Theme property that
REM was never declared, a file missing from the build's QML list - is
REM not a syntax error, passes qmllint, and reaches the user.
REM
REM This opens the real interface with no display attached and fails if
REM it complains. About a second.
REM ---- is every engine in the catalogue actually checked? -----------
REM
REM The sweep proves an engine DRAWS, and draws a picture no other engine
REM draws. That is not verification: a bump chart that ranked backwards
REM and a dial whose sweep ignored its scale would both pass it, and the
REM first drafts of two engines were wrong in exactly that way.
REM
REM So the catalogue carries a `verified` flag, written from the evidence
REM - a measured check in PlotSelfTest.cpp, or membership of the audited
REM baseline - and the library draws anything else in red. This is what
REM stops that flag drifting from the checks it claims to stand for.
REM
REM Stdlib only, so it runs wherever python does. Skipped, with a word,
REM where it does not.
echo   ... catalogue verification flags
where python >nul 2>&1
if errorlevel 1 (
  echo   [SKIP] python not on PATH - verified flags not checked
) else (
  python "%~dp0tools\mark_verified_engines.py" --check
  if errorlevel 1 (
    echo.
    echo   [WARNING] An engine is marked verified that nothing measures,
    echo             or one is measured and not marked. Run:
    echo               python tools\mark_verified_engines.py
    set "VERRC=1"
  ) else (
    echo   [OK] every engine's verified flag matches the evidence
  )
)

REM ---- the interface, actually exercised ---------------------------
REM
REM --selftest-ui below opens the window and lets it settle, which catches a
REM name that resolves to nothing. It does not press anything, and a QML
REM binding is only evaluated once something instantiates the component - so a
REM bool bound to `undefined`, a delegate reading a property it was never
REM given, and a panel that stages a mapping and never applies it are all
REM invisible to it. All three have shipped.
REM
REM These instantiate the real panels with stand-in data and assert what
REM happens. They need no build at all - they read the .qml straight out of the
REM source tree - so they run even when the build above failed.
echo   ... interface tests
python "%~dp0tools\run_ui_tests.py"
if errorlevel 1 (
  echo   [WARNING] An interface test failed. See the lines above.
  set "UITESTRC=1"
)

echo   ... interface
"%~dp0build\stage\graphvis.exe" --selftest-ui -platform offscreen
if errorlevel 1 (
  echo.
  echo   [WARNING] The interface reported warnings while starting.
  echo             See the startup log named above. The build is fine;
  echo             something in the QML is referring to a name that
  echo             does not exist.
  set "UIRC=1"
) else (
  echo   [OK] interface loaded clean
)

REM THE LOG THE REPORT READS MUST BE THE LOG THIS RUN WROTE.
REM
REM CHECK-GRAPHS.bat copies the %LOCALAPPDATA% logs into graph-check\ during
REM its step [2/3] above - which happens BEFORE --selftest-ui runs. So the
REM startup.log the report went on to judge was always from an EARLIER
REM session, never from the interface check it was describing. That is how a
REM build whose every launch died with an access violation was reported as
REM "problems: none", and its trend line read "better  startup.log problems
REM 2 -> 0 (down)": the report was reading a log written by something else.
REM
REM Copied again here, after the interface has actually been started, so the
REM report describes this run. Kept as a copy rather than by pointing the
REM report at %LOCALAPPDATA% because graph-check\ is the directory that gets
REM attached to a bug report, and a report citing a file nobody sent is not
REM evidence.
for %%D in (
  "%LOCALAPPDATA%\GraphVis\GraphVis 18.4\18.4\logs"
  "%LOCALAPPDATA%\GraphVis\18.4\logs"
  "%LOCALAPPDATA%\GraphVis\GraphVis 18.4\logs"
) do (
  if exist "%%~D\startup.log" copy /y "%%~D\startup.log" "%~dp0graph-check\" >nul 2>&1
)

REM ---- one page, for a person or for Claude ------------------------
REM
REM Everything above writes its own log and between them they come to about
REM eighty thousand characters, most of which is 439 lines saying "rendering
REM <engine>". This reduces it to the verdict, the failures verbatim, and what
REM CHANGED since the previous run - which is the part that says whether a
REM problem is new or has been there all week.
REM
REM Always at build-reports\REPORT.md, so it can be found without hunting.
echo.
echo   ... writing build-reports\REPORT.md
where python >nul 2>&1
if errorlevel 1 (
  echo   [SKIP] python not on PATH - no report written
) else (
  python "%~dp0tools\build_report.py"
)

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
