param(
  [string]$VcpkgRoot,
  [switch]$SkipInstaller,
  [switch]$Fast
)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path

# The pinned checkout wins over whatever VCPKG_ROOT happens to say. Visual
# Studio sets that variable to its OWN bundled vcpkg, which has a different
# version database from the baseline in vcpkg.json - and it contains a real
# vcpkg.exe, so the existence check below would happily accept it and build the
# release against the wrong dependency set. Only an explicit -VcpkgRoot
# overrides the pinned tree.
if(-not $VcpkgRoot){
  $pinned=Join-Path $Root '.tooling\vcpkg'
  if(Test-Path (Join-Path $pinned 'vcpkg.exe')){ $VcpkgRoot=$pinned }
  else { $VcpkgRoot=$env:VCPKG_ROOT }
}
$Version=(Get-Content (Join-Path $Root 'VERSION') -Raw).Trim()
$Preset=if($Fast){'windows-fast'}else{'windows-release'}
# `if` is a statement, not an expression, so it cannot sit bare inside the
# parentheses of an argument. It needs $( ) to be evaluated as a subexpression.
$Build=Join-Path $Root $(if($Fast){'build/windows-fast'}else{'build/windows-release'})
$Stage=Join-Path $Root $(if($Fast){'build/stage-fast'}else{'build/stage'})
function Require($name){ if(-not (Get-Command $name -ErrorAction SilentlyContinue)){throw "Missing required developer tool: $name"} }
Require cmake; Require ninja; Require cargo

# This script itself could not be parsed for months, and the only way anyone
# would have found out is by trying to cut a release. Check every script before
# spending twenty minutes on a build.
& (Join-Path $PSScriptRoot 'Check-Scripts.ps1') -Quiet
if($LASTEXITCODE -ne 0){throw 'One or more PowerShell scripts failed to parse - see above'}
if(-not $VcpkgRoot -or -not (Test-Path (Join-Path $VcpkgRoot 'vcpkg.exe'))){throw 'Set VCPKG_ROOT to the pinned vcpkg checkout. End users do not need vcpkg.'}
$env:VCPKG_ROOT=$VcpkgRoot
. (Join-Path $PSScriptRoot 'Build-Environment.ps1')
Initialize-GraphVisBuildEnvironment -Root $Root -NoCache:([bool]$env:GRAPHVIS_DISABLE_BUILD_CACHE)
Assert-GraphVisLockfiles -Root $Root

Write-Host '== Restore pinned native dependency graph (binary-cache aware) ==' -ForegroundColor Cyan
& (Join-Path $VcpkgRoot 'vcpkg.exe') install --triplet x64-windows --x-manifest-root=$Root
if($LASTEXITCODE -ne 0){throw 'vcpkg install failed'}
cargo fetch --locked --manifest-path (Join-Path $Root 'native/Cargo.toml')
if($LASTEXITCODE -ne 0){throw 'cargo fetch --locked failed'}

Write-Host "== Configure/build GraphVis $Version ($Preset) ==" -ForegroundColor Cyan
cmake --preset $Preset
if($LASTEXITCODE -ne 0){throw 'CMake configure failed'}
cmake --build --preset $Preset --parallel
if($LASTEXITCODE -ne 0){throw 'CMake build failed'}
if(Test-Path $Stage){Remove-Item $Stage -Recurse -Force}
cmake --install $Build --prefix $Stage
if($LASTEXITCODE -ne 0){throw 'CMake install/stage failed'}

# Runtime maintenance is internal to the installed app; it is never an end-user entry point.
$internal=Join-Path $Stage 'internal'; New-Item -ItemType Directory -Force -Path $internal|Out-Null
Copy-Item (Join-Path $Root 'tools/Install-State.ps1') $internal -Force
Copy-Item (Join-Path $Root 'packaging/internal/Runtime-Maintenance.ps1') $internal -Force

if(-not $Fast){
  & (Join-Path $PSScriptRoot 'Generate-RuntimeManifest.ps1') -Stage $Stage
  if($LASTEXITCODE -ne 0){throw 'Runtime manifest generation failed'}
}

if(-not $Fast -and -not $SkipInstaller){
  Require makensis
  $dist=Join-Path $Root 'dist'
  $name="GraphVis-$Version-Windows"
  $out=Join-Path $dist $name
  $zip=Join-Path $dist "$name.zip"
  if(Test-Path $out){Remove-Item $out -Recurse -Force}
  if(Test-Path $zip){Remove-Item $zip -Force}
  New-Item -ItemType Directory -Force -Path (Join-Path $out 'Support')|Out-Null

  Write-Host '== Compile NSIS Install.exe ==' -ForegroundColor Cyan
  & makensis "/DGraphVisStage=$Stage" "/DGraphVisOutputDir=$out" (Join-Path $Root 'installer/windows/GraphVis.nsi')
  if($LASTEXITCODE -ne 0){throw "NSIS/makensis failed with exit code $LASTEXITCODE"}

  Copy-Item (Join-Path $Root 'packaging/end_user/Install.bat') (Join-Path $out 'Install.bat') -Force
  Copy-Item (Join-Path $Root 'packaging/end_user/Uninstall.bat') (Join-Path $out 'Uninstall.bat') -Force
  Copy-Item (Join-Path $Root 'packaging/windows/README-WINDOWS.md') (Join-Path $out 'README.md') -Force
  Copy-Item (Join-Path $Root 'packaging/end_user/Repair.bat') (Join-Path $out 'Support/Repair.bat') -Force
  Copy-Item (Join-Path $Root 'packaging/end_user/Uninstall.bat') (Join-Path $out 'Support/Uninstall.bat') -Force

  foreach($required in @('Install.exe','Install.bat','Uninstall.bat','README.md','Support/Repair.bat','Support/Uninstall.bat')){
    if(-not (Test-Path (Join-Path $out $required))){throw "Public Windows release is missing: $required"}
  }
  Compress-Archive -Path $out -DestinationPath $zip -CompressionLevel Optimal
  Write-Host "Windows public release ready: $zip" -ForegroundColor Green
}
Write-Host "GraphVis native build complete: $Stage" -ForegroundColor Green
if(Get-Command sccache -ErrorAction SilentlyContinue){ sccache --show-stats }
