param([switch]$IncludeFlight)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $Root 'scripts\Enable-FastBuild.ps1')
if(-not (Test-Path (Join-Path $Root 'native\Cargo.lock'))){throw 'native/Cargo.lock is required before prewarming caches.'}
if(-not $env:VCPKG_ROOT){$env:VCPKG_ROOT=Join-Path $Root '.tooling\vcpkg'}
$Vcpkg=Join-Path $env:VCPKG_ROOT 'vcpkg.exe'
if(-not (Test-Path $Vcpkg)){throw 'Pinned vcpkg is missing. Run install.bat once.'}
Write-Host 'Prewarming vcpkg binary packages...' -ForegroundColor Cyan
& $Vcpkg install --triplet x64-windows --x-manifest-root=$Root
if($LASTEXITCODE -ne 0){throw 'vcpkg prewarm failed'}
Write-Host 'Prewarming Rust dependency graph...' -ForegroundColor Cyan
pushd (Join-Path $Root 'native')
cargo fetch --locked
if($LASTEXITCODE -ne 0){throw 'cargo fetch failed'}
$pkgs=@('-p','graphvis-ffi')
if($IncludeFlight){$pkgs += @('-p','graphvis-flight')}
cargo build --locked --profile fast @pkgs
if($LASTEXITCODE -ne 0){throw 'Rust prewarm build failed'}
popd
if(Get-Command sccache -ErrorAction SilentlyContinue){sccache --show-stats}
Write-Host 'GraphVis developer caches are prewarmed.' -ForegroundColor Green
