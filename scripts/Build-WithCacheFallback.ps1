param(
  [switch]$Fast,
  [switch]$SkipInstaller
)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BuildScript=Join-Path $Root 'tools\Build-Release.ps1'

function Invoke-Build([bool]$NoCache) {
  if($NoCache){
    Write-Warning 'Retrying with all disposable compiler/package caches disabled.'
    $env:GRAPHVIS_DISABLE_BUILD_CACHE='1'
    Remove-Item Env:RUSTC_WRAPPER -ErrorAction SilentlyContinue
    Remove-Item Env:CMAKE_C_COMPILER_LAUNCHER -ErrorAction SilentlyContinue
    Remove-Item Env:CMAKE_CXX_COMPILER_LAUNCHER -ErrorAction SilentlyContinue
    $env:VCPKG_BINARY_SOURCES='clear'
    foreach($p in @('.cache\cargo-target','.cache\sccache','.cache\vcpkg-binaries')){
      $full=Join-Path $Root $p
      if(Test-Path $full){Remove-Item $full -Recurse -Force}
    }
  } else {
    Remove-Item Env:GRAPHVIS_DISABLE_BUILD_CACHE -ErrorAction SilentlyContinue
  }
  & $BuildScript -VcpkgRoot $env:VCPKG_ROOT -SkipInstaller:$SkipInstaller -Fast:$Fast
  if($LASTEXITCODE -ne 0){ throw "GraphVis build failed with exit code $LASTEXITCODE" }
}

try { Invoke-Build $false }
catch {
  Write-Warning "Cached build failed: $($_.Exception.Message)"
  Invoke-Build $true
}
