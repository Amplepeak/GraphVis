@echo off
setlocal EnableExtensions
cd /d "%~dp0"
REM One page saying what the last build did, what is wrong with it, and what
REM changed since the run before. Reads graph-check\, build\stage\ and the
REM catalogue; builds nothing and deletes nothing.
REM
REM BUILD-AND-CHECK.bat runs this for you at the end of every run. This script
REM is for regenerating the page from the LAST run without rebuilding - after
REM pulling changes, or when the report has been read and you want it again.
where python >nul 2>&1
if errorlevel 1 (
  echo [ERROR] python is not on PATH, so the report cannot be written.
  pause
  exit /b 1
)
python "%~dp0tools\build_report.py" --print
echo.
echo ---- build-reports\REPORT.md ----
pause
