@echo off
REM Answers "what is the 133 MB actually made of?" - a question that has been
REM investigated twice and never measured, because this command has never run.
REM
REM READ-ONLY as far as the project goes: it builds in release and reports
REM sizes. It does not change any source file. It DOES install cargo-bloat the
REM first time (a developer tool, into your cargo bin, not into GraphVis) and
REM it will take a while on the first run because it needs a release build.
REM
REM Writes graph-check\cargo-bloat.txt.
cd /d "%~dp0native"
if not exist ..\graph-check mkdir ..\graph-check
set OUT=..\graph-check\cargo-bloat.txt

echo Checking for cargo-bloat...
cargo bloat --version >nul 2>&1
if errorlevel 1 (
  echo   not installed - installing it now, this takes a few minutes
  cargo install cargo-bloat
)

echo === cargo bloat, by crate === > "%OUT%"
cargo bloat --release --crates -n 40 >> "%OUT%" 2>&1

echo. >> "%OUT%"
echo === cargo bloat, by function === >> "%OUT%"
cargo bloat --release -n 40 >> "%OUT%" 2>&1

echo. >> "%OUT%"
echo === what the built artefacts weigh === >> "%OUT%"
dir /-c target\release\*.dll >> "%OUT%" 2>&1
dir /-c target\release\*.lib >> "%OUT%" 2>&1

echo.
echo Wrote graph-check\cargo-bloat.txt
type "%OUT%"
pause
