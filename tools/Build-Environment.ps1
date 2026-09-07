Set-StrictMode -Version Latest

function Initialize-GraphVisMsvcEnvironment {
  # Quick-Build.ps1 and friends invoke cl.exe through Ninja. cl.exe finds the
  # C++ standard library and the Windows SDK through the INCLUDE/LIB/PATH
  # environment variables that vcvars64.bat sets -- they are NOT passed on the
  # command line. A shell that has not entered the Visual Studio environment
  # therefore fails on the very first translation unit with:
  #   qglobal.h(14): fatal error C1083:
  #     Cannot open include file: 'type_traits': No such file or directory
  # That is what happens when build.bat / run.bat are double-clicked from
  # Explorer instead of being run from a Developer Command Prompt.
  # Import the environment here so either way works.
  if ($env:INCLUDE -and $env:LIB -and (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    Write-Host "MSVC environment: already initialised" -ForegroundColor DarkGray
    return
  }

  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  $vsPath = $null
  if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -prerelease -products * `
                -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                -property installationPath 2>$null | Select-Object -First 1
  }
  if (-not $vsPath) {
    foreach ($root in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
      if (-not $root) { continue }
      $cand = Get-ChildItem (Join-Path $root 'Microsoft Visual Studio') -Directory -ErrorAction SilentlyContinue |
              Sort-Object Name -Descending |
              ForEach-Object { Get-ChildItem $_.FullName -Directory -ErrorAction SilentlyContinue } |
              Where-Object { Test-Path (Join-Path $_.FullName 'VC\Auxiliary\Build\vcvars64.bat') } |
              Select-Object -First 1
      if ($cand) { $vsPath = $cand.FullName; break }
    }
  }
  if (-not $vsPath) {
    throw 'Visual Studio with the "Desktop development with C++" workload was not found. Install VS 2022 (or newer) Build Tools with that workload, then run this again. See START_HERE.txt.'
  }

  $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
  if (-not (Test-Path $vcvars)) {
    throw "Found Visual Studio at $vsPath but not its vcvars64.bat. The 'Desktop development with C++' workload is probably missing."
  }

  Write-Host "MSVC environment: importing from $vcvars" -ForegroundColor Cyan
  $captured = 0
  # vcvars64.bat sets VCPKG_ROOT to Visual Studio's OWN bundled vcpkg, and this
  # loop imports every variable it sets - so importing the MSVC environment
  # silently replaced the pinned vcpkg that the caller had just selected.
  #
  # That matters far more than the warning it produces. CMakePresets.json builds
  # CMAKE_TOOLCHAIN_FILE out of $env{VCPKG_ROOT}, so a FRESH configure after
  # this import would use Visual Studio's vcpkg - a different version, a
  # different baseline and a different dependency set - instead of the pinned
  # one in .tooling. It only ever appeared to work because an existing
  # CMakeCache.txt keeps the toolchain path it was first configured with, and
  # because a shell that had already entered the VS environment takes the early
  # return above and never reaches this loop.
  $pinnedVcpkgRoot = $env:VCPKG_ROOT

  cmd.exe /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') {
      try { Set-Item -LiteralPath ("Env:\" + $matches[1]) -Value $matches[2] -ErrorAction Stop; $captured++ } catch { }
    }
  }
  if (-not $env:INCLUDE) {
    throw "vcvars64.bat ran but INCLUDE is still empty. The Visual Studio installation at $vsPath looks broken."
  }

  # Put the caller's choice back. This also silences vcpkg's "ignoring
  # mismatched VCPKG_ROOT environment value" warning, because there is no
  # longer a mismatch to ignore.
  if ($pinnedVcpkgRoot) {
    if ($env:VCPKG_ROOT -ne $pinnedVcpkgRoot) {
      Write-Host "MSVC environment: kept pinned VCPKG_ROOT ($pinnedVcpkgRoot)" -ForegroundColor DarkGray
    }
    $env:VCPKG_ROOT = $pinnedVcpkgRoot
  }
  Write-Host "MSVC environment: ready ($captured variables)" -ForegroundColor Green
}

function Initialize-GraphVisBuildEnvironment {
  param(
    [Parameter(Mandatory=$true)][string]$Root,
    [switch]$NoCache
  )
  Initialize-GraphVisMsvcEnvironment

  $cache=Join-Path $Root '.cache'
  New-Item -ItemType Directory -Force $cache | Out-Null
  $env:CARGO_TARGET_DIR=Join-Path $cache 'cargo-target'
  $env:SCCACHE_DIR=Join-Path $cache 'sccache'
  New-Item -ItemType Directory -Force $env:CARGO_TARGET_DIR,$env:SCCACHE_DIR | Out-Null

  if($NoCache -or $env:GRAPHVIS_DISABLE_BUILD_CACHE){
    $env:VCPKG_BINARY_SOURCES='clear'
    Remove-Item Env:RUSTC_WRAPPER -ErrorAction SilentlyContinue
    Remove-Item Env:CMAKE_C_COMPILER_LAUNCHER -ErrorAction SilentlyContinue
    Remove-Item Env:CMAKE_CXX_COMPILER_LAUNCHER -ErrorAction SilentlyContinue
    Remove-Item Env:CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_LINKER -ErrorAction SilentlyContinue
    # Clean verification uses the platform linker as well as empty compiler/package caches.
    if($env:RUSTFLAGS){ $env:RUSTFLAGS=($env:RUSTFLAGS -replace '\s*-C linker-flavor=lld-link','').Trim() }
  } else {
    $vcpkgCache=Join-Path $cache 'vcpkg-binaries'
    New-Item -ItemType Directory -Force $vcpkgCache | Out-Null
    $env:VCPKG_BINARY_SOURCES="clear;files,$vcpkgCache,readwrite"
    if(Get-Command sccache -ErrorAction SilentlyContinue){
      $env:RUSTC_WRAPPER='sccache'
      $env:CMAKE_C_COMPILER_LAUNCHER='sccache'
      $env:CMAKE_CXX_COMPILER_LAUNCHER='sccache'
    }
  }

  # Rust-only Windows linker acceleration. `rust-lld` is part of the pinned Rust toolchain.
  # If the probe fails we deliberately retain MSVC link.exe semantics.
  if(-not $NoCache){
    $lld=Get-ChildItem "$env:USERPROFILE\.rustup\toolchains" -Filter 'rust-lld.exe' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if($lld){
      & $lld.FullName '-flavor' 'link' '/?' *> $null
      if($LASTEXITCODE -eq 0 -or $LASTEXITCODE -eq 1){
        $env:CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_LINKER=$lld.FullName
        if($env:RUSTFLAGS){ $env:RUSTFLAGS="$env:RUSTFLAGS -C linker-flavor=lld-link" }
        else { $env:RUSTFLAGS='-C linker-flavor=lld-link' }
      }
    }
  }
}

function Assert-GraphVisLockfiles {
  param([Parameter(Mandatory=$true)][string]$Root)
  $lock=Join-Path $Root 'native\Cargo.lock'
  if(-not (Test-Path $lock)){
    throw 'native/Cargo.lock is required for deterministic builds. Run scripts/Generate-And-Verify-Lockfile.ps1, review it, and commit it.'
  }
  $vcpkg=Get-Content (Join-Path $Root 'vcpkg.json') -Raw | ConvertFrom-Json
  if(-not $vcpkg.'builtin-baseline'){ throw 'vcpkg.json must contain builtin-baseline.' }

  # The checked-out vcpkg must BE the baseline. vcpkg resolves each dependency
  # to the version recorded at the baseline commit, then looks that version up
  # in the checked-out version database - so if the tree is older, every single
  # package fails with "no version database entry for <pkg> at <version>" and
  # none of those messages mentions the baseline. Say it once, clearly, here.
  $vcpkgDir=Join-Path $Root '.tooling\vcpkg'
  if(Test-Path (Join-Path $vcpkgDir '.git')){
    $head=(& git -C $vcpkgDir rev-parse HEAD 2>$null)
    if($LASTEXITCODE -eq 0 -and $head -and $head.Trim() -ne $vcpkg.'builtin-baseline'){
      throw ("Pinned vcpkg is at {0} but vcpkg.json's builtin-baseline is {1}. " -f $head.Trim().Substring(0,10), $vcpkg.'builtin-baseline'.Substring(0,10)) +
            'Every package will fail with "no version database entry". Run install.bat to re-pin it.'
    }
  }
}
