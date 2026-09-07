$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'Dev-State.ps1')
$check=Test-GraphVisDevState -Root $Root
if(-not $check.Ok){throw "Developer bootstrap is incomplete or stale:`n - "+($check.Errors -join "`n - ")+"`nRun install.bat once."}
& (Join-Path $PSScriptRoot 'Quick-Build.ps1')
if($LASTEXITCODE -ne 0){throw "Incremental build failed with exit code $LASTEXITCODE"}
$exe=Join-Path $Root 'build\stage-fast\graphvis.exe'
if(-not (Test-Path $exe)){throw "Build succeeded but graphvis.exe is missing: $exe"}
$logDir=Join-Path $env:LOCALAPPDATA 'GraphVis\18.4\logs'; New-Item -ItemType Directory -Force $logDir|Out-Null
$p=Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe -Parent) -PassThru
Start-Sleep -Milliseconds 1800; $p.Refresh()
if($p.HasExited){
  $startup=Join-Path $logDir 'startup.log'
  throw "GraphVis exited during startup with code $($p.ExitCode). Startup log: $startup"
}
Write-Host 'GraphVis developer build launched successfully.' -ForegroundColor Green
