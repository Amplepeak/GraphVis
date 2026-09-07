param(
  # Domain formats with heavy dependencies: mass spec, astronomy, seismic,
  # neuro, DICOM, geospatial, Access, DuckDB. INSTALL-DATA-FORMATS.bat now
  # passes this by default - the decision was that the full set is what this
  # build is for - and offers /lite to leave it out.
  #
  # Note what this switch still does NOT install: the [vlm] extra, which is
  # transformers plus torch. That is not a dataset format, it is the vision
  # model for reading charts out of papers, and torch alone is larger than
  # every other extra combined. It stays a separate, deliberate install.
  [switch]$AllFormats
)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if(-not (Get-Command py.exe -ErrorAction SilentlyContinue)){
  throw 'Python is optional in GraphVis 18, but dataset formats beyond CSV/Parquet/Arrow (MATLAB, HDF5, NetCDF, Excel...) need it. Install Python 3.12 and run this again.'
}
$EnvDir=Join-Path $env:LOCALAPPDATA 'GraphVis\18.4\python-science'
if(-not (Test-Path (Join-Path $EnvDir 'Scripts\python.exe'))){ & py -3.12 -m venv $EnvDir }
& (Join-Path $EnvDir 'Scripts\python.exe') -m pip install --upgrade pip

# [io] carries the readers most scientific users need, MATLAB included.
# [marine] rides along with the default install: two of its three packages are
# small and the readers it enables (CTD casts, dive computers, GPS tracks) are
# core to the ocean and diving work this build is used for.
$extras = if($AllFormats){'[literature,io,marine,io_extra]'}else{'[literature,io,marine]'}

# The requirement must be built as ONE string.
#
# Writing `(Join-Path ...)$extras` looks like concatenation but PowerShell
# parses it as two separate arguments, so pip received the path and then
# "[literature,io,marine]" on its own and rejected the second as an invalid
# requirement. Concatenate explicitly and pass a single argument.
$target = (Join-Path $Root 'services\python') + $extras
Write-Host "Installing GraphVis science service $extras" -ForegroundColor Cyan
Write-Host "  $target" -ForegroundColor DarkGray
& (Join-Path $EnvDir 'Scripts\python.exe') -m pip install $target
if($LASTEXITCODE -ne 0){
  # A single unavailable wheel should not cost the user every other format, so
  # fall back to installing the extras one at a time and report what failed.
  Write-Host "`nCombined install failed - retrying each format group on its own." -ForegroundColor Yellow
  $failed = @()
  foreach($group in $extras.Trim('[',']').Split(',')){
    $one = (Join-Path $Root 'services\python') + "[$group]"
    Write-Host "  installing [$group]" -ForegroundColor DarkGray
    & (Join-Path $EnvDir 'Scripts\python.exe') -m pip install $one
    if($LASTEXITCODE -ne 0){ $failed += $group }
  }
  if($failed.Count -gt 0){
    Write-Host ("`nThese format groups could not be installed: {0}" -f ($failed -join ', ')) -ForegroundColor Yellow
    Write-Host "Everything else is installed and usable." -ForegroundColor Yellow
  }
}

# Report what actually became importable, so a partial install is visible.
$probe = @'
import importlib
mods = {"MATLAB / HDF5":"h5py","NetCDF":"xarray","Excel":"openpyxl","Excel (legacy)":"xlrd",
        "OpenDocument":"odf","Excel binary":"pyxlsb","HTML/XML":"lxml","YAML":"yaml",
        "TDMS":"nptdms","Audio":"soundfile","SPSS":"pyreadstat","Well logs":"lasio",
        "Mass spec":"pyteomics","Flow cytometry":"fcsparser","Astronomy":"astropy",
        "ROOT":"uproot","Seismic":"obspy","EDF":"pyedflib","MNE":"mne","Axon ABF":"pyabf",
        "NWB":"pynwb","DICOM":"pydicom","NIfTI":"nibabel","TIFF":"tifffile",
        "Geospatial":"geopandas","JCAMP-DX":"jcamp",
        "dBase":"dbfread","Avro":"fastavro","MessagePack":"msgpack",
        "OFX / QFX":"ofxtools","SWIFT MT940":"mt940","Word tables":"docx",
        "PDF tables":"pdfplumber","DuckDB":"duckdb","R data":"pyreadr",
        "Apple Numbers":"numbers_parser","Access":"pyodbc",
        "Current profilers":"dolfyn","GRIB":"cfgrib","Garmin FIT":"fitparse"}
for label, mod in mods.items():
    try:
        importlib.import_module(mod); print(f"  available  {label}")
    except Exception:
        print(f"  missing    {label}  (pip install {mod})")
'@
Write-Host "`nDataset formats:" -ForegroundColor Cyan
$probe | & (Join-Path $EnvDir 'Scripts\python.exe') -
Write-Host "`nGraphVis science service installed to $EnvDir" -ForegroundColor Green
