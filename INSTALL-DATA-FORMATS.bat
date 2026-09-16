@echo off
setlocal EnableExtensions
cd /d "%~dp0"
echo.
echo   GraphVis science components
echo   ---------------------------
echo.
echo   GraphVis plots CSV, Parquet and Arrow on its own. Everything below is
echo   optional, and anything you skip can be added later from Add-ons inside
echo   the application without reinstalling.
echo.
echo   Everyday formats     MATLAB .mat, HDF5, NetCDF, Excel, TDMS, business
echo   Marine and diving    CTD, ADCP, RBR, NMEA, GPX, dive computers
echo   Literature           text, tables and numbers out of published PDFs
echo   Reports              Word and PDF batch reports
echo   Symbolic maths       exact derivatives, models typed as expressions
echo   Dimensional analysis mA/cm2 and mg/L/h converted properly
echo   Uncertainty          carry a fit's errors into what you calculate
echo   Domain formats       mass spec, astronomy, seismic, neuro, DICOM,
echo                        geospatial, Access, DuckDB
echo   Survival analysis    Kaplan-Meier, log-rank, Cox   (a large download)
echo   Chart reading        a vision model that reads data back out of a
echo                        published figure  (~2.8 GB, and slow to load)
echo.
echo   1  Everything except chart reading            (recommended)
echo   2  Everything, chart reading included
echo   3  Everyday formats, marine, literature and reports only
echo   4  Let me choose each one
echo.
set "GVCHOICE="
set /p "GVCHOICE=Choose 1-4 (Enter for 1): "
if not defined GVCHOICE set "GVCHOICE=1"

if "%GVCHOICE%"=="1" set "GVKEYS=io,marine,literature,reports,symbolic,units,uncertainty,io_extra"
if "%GVCHOICE%"=="2" set "GVKEYS=io,marine,literature,reports,symbolic,units,uncertainty,io_extra,survival,vlm"
if "%GVCHOICE%"=="3" set "GVKEYS=io,marine,literature,reports,symbolic,units,uncertainty"
if "%GVCHOICE%"=="4" goto choose
if not defined GVKEYS (
  echo Not one of 1-4. Using the recommended set.
  set "GVKEYS=io,marine,literature,reports,symbolic,units,uncertainty,io_extra"
)
goto install

:choose
set "GVKEYS="
call :ask io "Everyday scientific formats" Y
call :ask marine "Marine and diving instruments" Y
call :ask literature "Literature extraction" Y
call :ask reports "Word and PDF reports" Y
call :ask symbolic "Symbolic maths" Y
call :ask units "Dimensional analysis" Y
call :ask uncertainty "Uncertainty propagation" Y
call :ask io_extra "Domain formats (large download)" Y
call :ask survival "Survival analysis" N
call :ask vlm "Chart reading (~2.8 GB)" N
if not defined GVKEYS (
  echo.
  echo Nothing selected, so nothing was installed.
  pause
  exit /b 0
)
goto install

:ask
set "GVANS="
set /p "GVANS=Install %~2? [%~3] "
if not defined GVANS set "GVANS=%~3"
if /i "%GVANS%"=="y" (
  if defined GVKEYS (set "GVKEYS=%GVKEYS%,%~1") else (set "GVKEYS=%~1")
)
exit /b 0

:install
echo.
echo Installing: %GVKEYS%
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Manage-OptionalComponents.ps1" -Install "%GVKEYS%"
echo.
echo Anything you skipped can be added later from Add-ons in the application.
pause
