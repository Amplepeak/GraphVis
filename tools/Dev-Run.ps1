param(
  # Force an incremental build before launching, even when the staged app is
  # already up to date.
  [switch]$Build,
  # Build with the release preset rather than the fast one, when building.
  [switch]$Full
)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'Dev-State.ps1')
$check=Test-GraphVisDevState -Root $Root
if(-not $check.Ok){throw "Developer bootstrap is incomplete or stale:`n - "+($check.Errors -join "`n - ")+"`nRun install.bat once."}

# ---------------------------------------------------------------------------
# Run what is built, and build only when there is a reason to.
#
# This used to run Quick-Build unconditionally and then launch
# build\stage-fast\graphvis.exe. Two things were wrong with that, and together
# they cost a finished build:
#
#   * A completed RELEASE build stages to build\stage, not build\stage-fast. So
#     run.bat ignored a working graphvis.exe that had just been produced and
#     started configuring a second, separate build tree - which is what filled
#     the disk and failed.
#   * "Run" should run. Building on every launch means every launch can fail
#     for reasons that have nothing to do with running the program.
#
# So: find the staged builds, take the newest, and launch it. Build first only
# if there is no staged build at all, if a source file is newer than the
# staged executable, or if -Build was asked for.
# ---------------------------------------------------------------------------
$staged=@(
  [pscustomobject]@{ Name='release'; Exe=(Join-Path $Root 'build\stage\graphvis.exe') },
  [pscustomobject]@{ Name='fast';    Exe=(Join-Path $Root 'build\stage-fast\graphvis.exe') }
) | Where-Object { Test-Path $_.Exe } |
    ForEach-Object { $_ | Add-Member -NotePropertyName Built -NotePropertyValue (Get-Item $_.Exe).LastWriteTime -PassThru } |
    Sort-Object Built -Descending

$newest = if($staged){ $staged[0] } else { $null }

# The newest source file, so a staged build that predates an edit is not
# silently launched as if it were current. Only the trees that go into the
# executable; the build directories themselves are always newer and would make
# this always true.
$reason=$null
if($Build){ $reason='-Build was requested' }
elseif(-not $newest){ $reason='nothing is staged yet' }
else{
  $sources=Get-ChildItem -Path @(
      (Join-Path $Root 'app'),(Join-Path $Root 'native'),(Join-Path $Root 'services')
    ) -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Extension -in '.cpp','.h','.hpp','.qml','.rs','.py','.json','.txt' } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if($sources -and $sources.LastWriteTime -gt $newest.Built){
    $reason="$($sources.Name) is newer than the staged build"
  }
}

if($reason){
  Write-Host "Building: $reason." -ForegroundColor Cyan
  if($Full){ & (Join-Path $PSScriptRoot 'Quick-Build.ps1') -Full }
  else     { & (Join-Path $PSScriptRoot 'Quick-Build.ps1') }
  if($LASTEXITCODE -ne 0){throw "Incremental build failed with exit code $LASTEXITCODE"}
  $exe=Join-Path $Root ($(if($Full){'build\stage\graphvis.exe'}else{'build\stage-fast\graphvis.exe'}))
  if(-not (Test-Path $exe)){throw "Build succeeded but graphvis.exe is missing: $exe"}
}else{
  $exe=$newest.Exe
  $age=[int]((Get-Date)-$newest.Built).TotalMinutes
  Write-Host "Launching the staged $($newest.Name) build (built $age minute(s) ago). Use run.bat --build to rebuild first." -ForegroundColor DarkGray
}

$logDir=Join-Path $env:LOCALAPPDATA 'GraphVis\18.4\logs'; New-Item -ItemType Directory -Force $logDir|Out-Null
$p=Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe -Parent) -PassThru
Start-Sleep -Milliseconds 1800; $p.Refresh()
if($p.HasExited){
  $startup=Join-Path $logDir 'startup.log'
  throw "GraphVis exited during startup with code $($p.ExitCode). Startup log: $startup"
}
Write-Host "GraphVis launched: $exe" -ForegroundColor Green
