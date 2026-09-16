param(
  [switch]$Full,
  [switch]$InstallOnly
)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if ($Full) {
    $Preset = 'windows-release'
    $Build  = Join-Path $Root 'build/windows-release'
    $Stage  = Join-Path $Root 'build/stage'
} else {
    $Preset = 'windows-fast'
    $Build  = Join-Path $Root 'build/windows-fast'
    $Stage  = Join-Path $Root 'build/stage-fast'
}

$Vcpkg=Join-Path $Root '.tooling/vcpkg'

function Require($name){ if(-not (Get-Command $name -ErrorAction SilentlyContinue)){ throw "Missing $name. Run install.bat once." } }
Require cmake; Require ninja; Require cargo
if(-not (Test-Path (Join-Path $Vcpkg 'vcpkg.exe'))){ throw 'GraphVis developer toolchain is not initialized. Run install.bat once.' }
$env:VCPKG_ROOT=$Vcpkg
. (Join-Path $PSScriptRoot 'Build-Environment.ps1')
Initialize-GraphVisBuildEnvironment -Root $Root
Assert-GraphVisLockfiles -Root $Root

# Daily builds deliberately do NOT run winget, rustup updates, git fetch,
# cargo fetch, or vcpkg install.  The configured CMake tree and binary caches
# are reused exactly as-is.
#
# Pushed to $Root first. Both `cmake --preset` and `cmake --build --preset`
# resolve CMakePresets.json against the CURRENT directory, not against the
# project this script already went to the trouble of locating - so running it
# from anywhere else failed with "Could not read presets from <cwd>", after
# several seconds of reporting a correctly prepared build environment. It is a
# nasty one to read, because the message names a directory nobody asked to
# build and says nothing about the working directory being the problem.
Push-Location $Root
try {
  if(-not (Test-Path (Join-Path $Build 'CMakeCache.txt')) -and -not $InstallOnly){
    Write-Host "Configuring $Preset once (no toolchain update checks)..." -ForegroundColor Cyan
    cmake --preset $Preset
    # $ErrorActionPreference does not apply to native executables, so a failed
    # cmake here used to fall straight through to the install step below and
    # stage the previous binary while reporting success.
    if($LASTEXITCODE -ne 0){ throw "CMake configure failed ($Preset), exit code $LASTEXITCODE" }
  }
  if(-not $InstallOnly){
    Write-Host "Incremental GraphVis build ($Preset)..." -ForegroundColor Cyan
    cmake --build --preset $Preset --parallel
    if($LASTEXITCODE -ne 0){ throw "Build failed ($Preset), exit code $LASTEXITCODE - the staged app has NOT been updated" }
  }
}
finally { Pop-Location }
New-Item $Stage -ItemType Directory -Force | Out-Null
cmake --install $Build --prefix $Stage
if($LASTEXITCODE -ne 0){ throw "Install/stage failed, exit code $LASTEXITCODE" }
Write-Host "Quick build ready: $Stage" -ForegroundColor Green
if(Get-Command sccache -ErrorAction SilentlyContinue){sccache --show-stats}