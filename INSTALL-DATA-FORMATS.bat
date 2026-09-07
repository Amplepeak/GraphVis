@echo off
setlocal EnableExtensions
cd /d "%~dp0"
echo Installing the GraphVis science service.
echo This adds MATLAB (.mat), HDF5, NetCDF, Excel, TDMS, business and finance
echo files, and the marine set: CTD casts, ADCP, NMEA, GPX and dive computers.
echo GraphVis reads 149 dataset formats in total.
echo.
echo Pass /all to also install the heavy domain formats
echo (mass spec, astronomy, seismic, neuro, DICOM, geospatial, Access, DuckDB).
echo.
if /i "%~1"=="/all" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Install-OptionalPythonScience.ps1" -AllFormats
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Install-OptionalPythonScience.ps1"
)
echo.
pause
