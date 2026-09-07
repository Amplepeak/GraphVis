# Collect everything that explains a GraphVis failure, into one readable file.
#
# GraphVis writes a running log of every Qt message to
#   %LOCALAPPDATA%\GraphVis\GraphVis 18.4\18.4\logs\startup.log
# which lives outside the project folder. This copies it, plus the Windows
# error records for graphvis.exe, into C:\GraphVis\crash-report.txt.
$ErrorActionPreference = 'Continue'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Out  = Join-Path $Root 'crash-report.txt'
$logDir = Join-Path $env:LOCALAPPDATA 'GraphVis\GraphVis 18.4\18.4\logs'
$startup = Join-Path $logDir 'startup.log'

"GraphVis crash report - $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Set-Content $Out -Encoding utf8
"=" * 72 | Add-Content $Out -Encoding utf8

Add-Content $Out "`n## Build" -Encoding utf8
foreach ($exe in @('build\stage-fast\graphvis.exe','build\windows-fast\graphvis.exe')) {
  $p = Join-Path $Root $exe
  if (Test-Path $p) { Add-Content $Out ("{0}  {1}" -f $exe, (Get-Item $p).LastWriteTime) -Encoding utf8 }
}

Add-Content $Out "`n## Qt message log ($startup)" -Encoding utf8
if (Test-Path $startup) {
  Get-Content $startup -Tail 400 | Add-Content $Out -Encoding utf8
} else {
  Add-Content $Out "(no startup.log - GraphVis has not run, or died before Qt started)" -Encoding utf8
}

Add-Content $Out "`n## Windows application errors for graphvis.exe (last 24h)" -Encoding utf8
try {
  $since = (Get-Date).AddDays(-1)
  $events = Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=$since} -ErrorAction SilentlyContinue |
            Where-Object { $_.Message -match 'graphvis' -or $_.ProviderName -match 'Application Error|\.NET Runtime|Windows Error Reporting' } |
            Select-Object -First 25
  if ($events) {
    foreach ($e in $events) {
      Add-Content $Out ("`n[{0}] {1} (id {2})" -f $e.TimeCreated, $e.ProviderName, $e.Id) -Encoding utf8
      Add-Content $Out ($e.Message -split "`n" | Select-Object -First 12) -Encoding utf8
    }
  } else { Add-Content $Out "(no matching Windows error records)" -Encoding utf8 }
} catch {
  Add-Content $Out "(could not read the event log: $($_.Exception.Message))" -Encoding utf8
}

Add-Content $Out "`n## GPU / renderer" -Encoding utf8
Add-Content $Out ("QSG_RHI_BACKEND={0}  QT_OPENGL={1}" -f $env:QSG_RHI_BACKEND, $env:QT_OPENGL) -Encoding utf8
try {
  Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue |
    ForEach-Object { Add-Content $Out ("{0}  driver {1}" -f $_.Name, $_.DriverVersion) -Encoding utf8 }
} catch {}

Write-Host "Crash report written to $Out" -ForegroundColor Green
