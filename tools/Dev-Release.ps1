$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'Dev-State.ps1')
$check=Test-GraphVisDevState -Root $Root
if(-not $check.Ok){throw "Developer bootstrap is incomplete or stale:`n - "+($check.Errors -join "`n - ")+"`nRun install.bat once."}
$env:VCPKG_ROOT=Join-Path $Root '.tooling\vcpkg'
if(-not (Get-Command makensis -ErrorAction SilentlyContinue)){
  if(-not (Get-Command winget -ErrorAction SilentlyContinue)){throw 'NSIS/makensis is missing and winget is unavailable. Install free NSIS package NSIS.NSIS.'}
  Write-Host 'Installing free NSIS compiler for release packaging...' -ForegroundColor Cyan
  winget install --id NSIS.NSIS -e --silent --accept-package-agreements --accept-source-agreements
  if($LASTEXITCODE -ne 0){throw 'NSIS installation failed'}
  $machine=[Environment]::GetEnvironmentVariable('Path','Machine'); $user=[Environment]::GetEnvironmentVariable('Path','User'); $env:Path=((@($machine,$user)|Where-Object{$_}) -join ';')
  foreach($dir in @('C:\Program Files (x86)\NSIS','C:\Program Files\NSIS')){ if(Test-Path (Join-Path $dir 'makensis.exe')){$env:Path="$dir;$env:Path";break} }
}
if(-not (Get-Command makensis -ErrorAction SilentlyContinue)){throw 'makensis.exe is still unavailable after NSIS setup'}
& (Join-Path $PSScriptRoot 'Build-Release.ps1')
