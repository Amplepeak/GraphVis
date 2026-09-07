param([switch]$Stage)
$ErrorActionPreference = 'Continue'
$Root  = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Dir   = if ($Stage) { Join-Path $Root 'build/stage-fast' } else { Join-Path $Root 'build/windows-fast' }
$exe   = Join-Path $Dir 'graphvis.exe'
Write-Host "Running: $exe"
if (-not (Test-Path $exe)) { Write-Host "[FAIL] not found"; exit 1 }

$logDir = Join-Path $env:LOCALAPPDATA 'GraphVis\GraphVis 18.4\18.4\logs'
$srcLog = Join-Path $logDir 'startup.log'
Remove-Item $srcLog -Force -ErrorAction SilentlyContinue

$env:QT_FORCE_STDERR_LOGGING = '1'
$env:QT_DEBUG_PLUGINS        = '1'
$env:QT_LOGGING_RULES        = 'qt.qml.import=true;qt.qpa.*=true;qt.scenegraph.*=true'

$p = Start-Process -FilePath $exe -WorkingDirectory $Dir -PassThru -ErrorAction SilentlyContinue
if (-not $p) { Write-Host "[FAIL] Could not start (loader refused - still an SxS/DLL problem)."; exit 1 }
if (-not $p.WaitForExit(25000)) {
  Write-Host "[OK] Still running after 25s. Killing it so the log can be collected."
  $p.Kill(); Start-Sleep 2
} else {
  Write-Host ("Exited with code 0x{0:X8} ({0})" -f $p.ExitCode)
}

Write-Host "`n=== startup.log ($srcLog) ==="
if (Test-Path $srcLog) {
  Copy-Item $srcLog (Join-Path $Root 'startup-log.txt') -Force
  Get-Content $srcLog | Write-Host
} else {
  Write-Host "[none written] - the app died before Qt initialised."
}

Write-Host "`n=== deployed QML plugin dir ==="
Get-ChildItem (Join-Path $Dir 'qml') -Recurse -ErrorAction SilentlyContinue |
  Select-Object -ExpandProperty FullName | Write-Host
