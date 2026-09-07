@echo off
setlocal EnableExtensions
cd /d "%~dp0"
echo Installing the GraphVis science service - all 149 dataset formats.
echo.
echo   MATLAB (.mat), HDF5, NetCDF, Excel, TDMS, business and finance files
echo   Marine: CTD casts, ADCP, NMEA, GPX, dive computers
echo   Domain: mass spec, astronomy, seismic, neuro, DICOM, geospatial,
echo           Access, DuckDB
echo.
echo This is a large download. Pass /lite for the everyday set only
echo (everything except the domain formats on the last two lines).
echo.
if /i "%~1"=="/lite" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Install-OptionalPythonScience.ps1"
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Install-OptionalPythonScience.ps1" -AllFormats
)
echo.
pause
