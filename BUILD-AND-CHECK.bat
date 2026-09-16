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

REM A CRASH IS NOT A WARNING, and this could not tell them apart.
REM
REM `if errorlevel 1` is true for both, so the build that died with an access
REM violation on every launch printed the same "[WARNING] the interface
REM reported warnings" as a build with one stray QML name - and then carried on
REM and exited 0. The exit code already says which happened:
REM
REM     0  loaded, walked, said nothing
REM     3  loaded and complained about a name       -> a warning
REM     1  QML produced no root object              -> a failure
REM  else  did not survive being started            -> a failure
REM
echo   ... interface
"%~dp0build\stage\graphvis.exe" --selftest-ui -platform offscreen
set "UIEXIT=%ERRORLEVEL%"
if "%UIEXIT%"=="0" (
  echo   [OK] interface loaded clean and walked
) else if "%UIEXIT%"=="3" (
  echo.
  echo   [WARNING] The interface reported warnings while starting.
  echo             See the startup log named above. The build is fine;
  echo             something in the QML is referring to a name that
  echo             does not exist.
  set "UIRC=1"
) else (
  echo.
  echo   [FAILED] The interface DID NOT SURVIVE being started ^(exit %UIEXIT%^).
  echo            That is a crash, not a warning. startup.log stops at the
  echo            last thing it managed to write; Windows records the faulting
  echo            module under Application Error in Event Viewer, and
  echo            COLLECT-CRASH-REPORT.bat gathers both.
  set "UICRASH=1"
)

REM THE LOG THE REPORT READS MUST BE THE LOG THIS RUN WROTE.
REM
REM CHECK-GRAPHS.bat copies the %LOCALAPPDATA% logs into graph-check\ during
REM its step [2/3] above - which happens BEFORE --selftest-ui runs. So the
REM startup.log the report went on to judge was always from an EARLIER session,
REM never from the interface check it was describing. That is how a build whose
REM every launch died with an access violation was reported as "problems:
REM none", and its trend line read "better  startup.log problems 2 -> 0 (down)":
REM the report was reading a log written by something else.
REM
REM This block has now been lost once, to a patch built from a stale copy of
REM this file, and the report's own staleness guard is what noticed. If it goes
REM missing again the symptom is the same: the report says it cannot tell
REM whether the interface started.
for %%D in (
  "%LOCALAPPDATA%\GraphVis\GraphVis 18.4\18.4\logs"
  "%LOCALAPPDATA%\GraphVis\18.4\logs"
  "%LOCALAPPDATA%\GraphVis\GraphVis 18.4\logs"
) do (
  if exist "%%~D\startup.log" copy /y "%%~D\startup.log" "%~dp0graph-check\" >nul 2>&1
)

REM ---- and again as somebody who has never run it ------------------
REM
REM EVERY CHECK ABOVE RUNS AS YOU. Your %LOCALAPPDATA% has settings, an arrow
REM cache, a scan cache and a previous session with a dataset in it, and the
REM interface restores all of that on the way up. A person installing this for
REM the first time has none of it, and that is a different path through the
REM same code: no cached dataset, no figure to restore, every default taken
REM rather than read back.
REM
REM Nobody has ever run it. It is also the only path that matters for a first
REM impression, which is the one thing a release cannot get a second go at.
REM
REM Done by pointing %LOCALAPPDATA% at an empty folder for one run. `setlocal`
REM at the top of this script keeps that change inside it, so your real profile
REM is untouched - and the app writes its log into the temporary one, which is
REM copied out below so the report and a person can both read it.
echo   ... interface, first run ^(empty profile^)
REM
REM POINTING %LOCALAPPDATA% SOMEWHERE ELSE DID NOT WORK, and did not say so.
REM That was the first attempt and it silently did nothing: on Windows
REM QStandardPaths asks the shell through SHGetKnownFolderPath rather than
REM reading that variable, so the app went on writing to the real profile, the
REM redirected folder stayed empty, and this step produced no log at all -
REM twice, without failing, because there was nothing to find.
REM
REM --first-run does it inside the app, where the path is actually decided:
REM Qt's test-mode location, emptied first. Nothing is renamed and the real
REM profile is never touched, so an interrupted run leaves nothing to restore.
"%~dp0build\stage\graphvis.exe" --selftest-ui --first-run -platform offscreen
set "FRESHEXIT=%ERRORLEVEL%"

REM The first run writes into the qttest profile; copy its log out beside the
REM ordinary one. Kept separate, because "works for you" and "works for someone
REM new" are two questions and the report should say which one failed.
for %%D in (
  "%LOCALAPPDATA%\qttest\GraphVis\GraphVis 18.4\18.4\logs"
  "%LOCALAPPDATA%\qttest\GraphVis\18.4\logs"
) do (
  if exist "%%~D\startup.log" copy /y "%%~D\startup.log" "%~dp0graph-check\startup-firstrun.log" >nul 2>&1
)

if "%FRESHEXIT%"=="0" (
  echo   [OK] first run clean
) else if "%FRESHEXIT%"=="3" (
  echo   [WARNING] The FIRST RUN reported warnings. See
  echo             graph-check\startup-firstrun.log
  set "FRESHRC=1"
) else (
  echo.
  echo   [FAILED] The interface DID NOT SURVIVE a first run ^(exit %FRESHEXIT%^).
  echo            It starts for you because your profile already has settings
  echo            and a cached dataset; a new install has neither.
  set "FRESHCRASH=1"
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

REM ---- the verdict, which this script used to collect and discard --------
REM
REM UIRC and UITESTRC were set by the two blocks above and then read by
REM nothing at all: every run ended `exit /b 0`, and the only trace of a
REM failure was a line that had scrolled past several minutes earlier. A flag
REM that is set and never read is a check that reports nothing - which is how
REM an interface that crashed on every launch finished this script looking
REM exactly like one that did not.
REM
REM So the end of the run says what failed, and the exit code means something.
set "VERDICT=0"
if defined UICRASH set "VERDICT=1"
if defined FRESHCRASH set "VERDICT=1"
if defined UITESTRC set "VERDICT=1"
if "%VERDICT%"=="0" (
  if defined UIRC (
    echo VERDICT: built and checked, with warnings from the interface.
  ) else if defined FRESHRC (
    echo VERDICT: built and checked, with warnings on a first run only.
  ) else (
    echo VERDICT: built and checked, nothing to report.
  )
) else (
  echo ================================================================
  echo  VERDICT: SOMETHING FAILED
  echo ================================================================
  if defined UICRASH echo   - the interface did not survive being started
  if defined FRESHCRASH echo   - the interface did not survive a FIRST run ^(empty profile^)
  if defined UITESTRC echo   - an interface test failed
  echo.
  echo  build-reports\REPORT.md has the detail.
)
echo.
pause
exit /b %VERDICT%
