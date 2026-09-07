$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'Dev-State.ps1')
Write-Host 'GraphVis 18.4 developer bootstrap' -ForegroundColor Cyan
Write-Host 'This is the only developer setup step. It is never used by end users.'
& (Join-Path $PSScriptRoot 'Bootstrap-WindowsBuild.ps1') -SkipInstaller -SetupOnly
if($LASTEXITCODE -ne 0){throw "Bootstrap-WindowsBuild failed with exit code $LASTEXITCODE"}
$path=Write-GraphVisDevState -Root $Root
$check=Test-GraphVisDevState -Root $Root
if(-not $check.Ok){Remove-Item $path -Force -ErrorAction SilentlyContinue; throw "Developer setup validation failed:`n - "+($check.Errors -join "`n - ")}
Write-Host "Developer setup complete and validated: $path" -ForegroundColor Green
