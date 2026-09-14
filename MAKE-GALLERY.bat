@echo off
setlocal EnableExtensions
cd /d "%~dp0"
echo ================================================================
echo  GraphVis - draw every engine and keep the pictures
echo ================================================================
echo.
echo The build already renders all 434 catalogue engines on every run
echo and throws the pictures away after checking each one drew
echo something. This keeps them: one PNG per engine, in gallery\ in
echo this folder. Nothing is sent anywhere.
echo.
echo "Did it draw anything" is a question a machine can answer, and it
echo is answered on every build. "Is the legend covering the data, is
echo that axis labelled in the wrong units, is this readable at 89 mm"
echo is not, and there is no way to judge 434 figures without looking
echo at them.
echo.

set OUT=%~dp0gallery
if exist "%OUT%" rmdir /s /q "%OUT%" 2>nul
mkdir "%OUT%" 2>nul

REM Same executable-finding order as CHECK-GRAPHS.bat, so the two never
REM disagree about which build they are describing.
set EXE=%~dp0build\stage\graphvis.exe
if not exist "%EXE%" set EXE=%~dp0build\stage-fast\graphvis.exe
if not exist "%EXE%" (
  echo [ERROR] No built graphvis.exe found under build\stage or build\stage-fast.
  echo Run build.bat first.
  pause
  exit /b 1
)
echo Using %EXE%
echo.
echo Rendering. This takes a couple of minutes.

REM --selftest-plot is required: the gallery flag rides along with the
REM normal self-test rather than being a second entry point, so the
REM pictures are always of the same render the checks just passed or
REM failed on. The PDF goes to the gallery folder to keep it together.
"%EXE%" --selftest-plot "%OUT%\selftest.pdf" --selftest-gallery "%OUT%" > "%OUT%\gallery-output.txt" 2>&1
set RC=%ERRORLEVEL%

echo.
echo   exit code %RC%
for /f %%N in ('dir /b "%OUT%\*.png" 2^>nul ^| find /c /v ""') do echo   %%N figures written

echo.
if "%RC%"=="0" (
  echo Done. Open "%OUT%" and look through them.
  echo The log is gallery-output.txt in the same folder.
) else (
  echo The run did not finish cleanly - see gallery-output.txt.
  echo Any figures written before it stopped are still in "%OUT%".
)
echo.
pause
