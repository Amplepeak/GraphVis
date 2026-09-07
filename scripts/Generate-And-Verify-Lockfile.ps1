$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Manifest=Join-Path $Root 'native\Cargo.toml'
if(-not (Get-Command cargo -ErrorAction SilentlyContinue)){throw 'Cargo is required.'}
cargo generate-lockfile --manifest-path $Manifest
if($LASTEXITCODE -ne 0){throw 'cargo generate-lockfile failed'}
cargo metadata --locked --manifest-path $Manifest --format-version 1 | Out-Null
if($LASTEXITCODE -ne 0){throw 'Generated Cargo.lock failed locked metadata validation'}
Write-Host 'native/Cargo.lock generated and validated. Review and COMMIT this file before any release build.' -ForegroundColor Green
