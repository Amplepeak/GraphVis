@echo off
setlocal EnableExtensions
cd /d "%~dp0"

REM Start GraphVis as somebody who has never run it.
REM
REM Every other way of launching this uses YOUR profile: settings, an arrow
REM cache, a scan cache and a previous session with a dataset in it, all of
REM which the interface restores on the way up. A new install has none of that,
REM and it is a different path through the same code - no cached dataset, no
REM figure to restore, every default taken rather than read back. It is also
REM the only path a release cannot get a second go at.
REM
REM YOUR REAL PROFILE IS NOT TOUCHED. The app is asked, on the command line, to
REM put its data somewhere of its own (Qt's test-mode location, a `qttest`
REM folder beside the normal one) and to empty that first. Nothing is renamed,
REM nothing is moved, and there is nothing to restore afterwards - so it is
REM safe to close the window at any point, including by killing it.
REM
REM An earlier attempt did this by pointing %LOCALAPPDATA% at a temporary
REM folder, which silently did nothing: on Windows QStandardPaths asks the
REM shell rather than reading that variable, so the app carried on writing to
REM the real profile and the check found an empty folder and reported nothing.

if not exist "build\stage\graphvis.exe" (
  echo [ERROR] build\stage\graphvis.exe is not there. Build first:
  echo         run.bat --build     or     BUILD-AND-CHECK.bat
  echo.
  pause
  exit /b 1
)

echo ================================================================
echo  GraphVis - FIRST RUN
echo ================================================================
echo.
echo  Starting with an empty profile. Everything you do in this window
echo  is thrown away; your normal settings and datasets are untouched.
echo.
echo  Worth trying, in the order a new person would:
echo    - does the window come up, and does it look finished
echo    - import a dataset          ^(File, or the Import button^)
echo    - pick a graph, map columns, change the engine
echo    - add a second figure in the notebook, then close it
echo    - export a figure, and write a dataset out
echo    - open Add-ons, Help, and the Publish workspace
echo.

"build\stage\graphvis.exe" --first-run
set "RC=%ERRORLEVEL%"

echo.
if "%RC%"=="0" (
  echo  Closed normally.
) else (
  echo  [ATTENTION] It exited with code %RC%, which is not a clean close.
  echo              If you did not close it yourself, that is a crash.
  echo              COLLECT-CRASH-REPORT.bat gathers what Windows recorded.
)
echo.
echo  The log for this run is under the `qttest` folder inside
echo    %LOCALAPPDATA%
echo  and is emptied again the next time you run this.
echo.
pause
exit /b %RC%
