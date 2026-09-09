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

# Emptying a folder that something else is using.
#
# The staging tree is deleted and rebuilt on every release build, and a plain
# Remove-Item on it fails with "Access to the path 'qgif.dll' is denied" the
# moment anything holds a file inside it open. That is a twenty-minute build
# thrown away at the last step, reported as a permissions error - which sends
# people to file properties and administrator prompts, when the cause is almost
# always that they are running the copy of GraphVis they just built.
#
# So: name the process holding it, because closing that is the entire fix. A
# DLL loaded into a running executable cannot be deleted by anyone at any
# privilege level, and no amount of -Force changes that.
function Clear-BuildTree {
  param([Parameter(Mandatory=$true)][string]$Path,[string]$What='staging folder')
  if(-not (Test-Path $Path)){ return }
  $full=(Resolve-Path $Path).Path
  # Anything running FROM inside the folder about to be deleted.
  $holders=@(Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $_.Path -and $_.Path.StartsWith($full,[System.StringComparison]::OrdinalIgnoreCase) })
  if($holders.Count -gt 0){
    $names=($holders | ForEach-Object { "$($_.ProcessName) (pid $($_.Id))" }) -join ', '
    throw "The $What is in use by $names. Close it and run the build again - a file loaded into a running program cannot be deleted by anything, an administrator included."
  }
  # A few tries anyway: an antivirus scan or an Explorer window on the folder
  # takes a handle for a moment and gives it back.
  for($attempt=1;$attempt -le 5;$attempt++){
    try { Remove-Item $Path -Recurse -Force -ErrorAction Stop; return }
    catch {
      if($attempt -eq 5){
        throw "Could not empty the $What at $full : $($_.Exception.Message). Something has a file in there open - usually GraphVis still running, an Explorer window showing the folder, or an unfinished antivirus scan."
      }
      Start-Sleep -Milliseconds (250*$attempt)
    }
  }
}
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
Clear-BuildTree -Path $Stage -What 'staging folder'
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
  Clear-BuildTree -Path $out -What 'release folder'
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
