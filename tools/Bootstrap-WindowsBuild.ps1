param([switch]$SkipInstaller,[switch]$Fast,[switch]$SetupOnly)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'Build-Environment.ps1')
function Have($n){ [bool](Get-Command $n -ErrorAction SilentlyContinue) }
function Refresh-ProcessPath {
  $machine=[Environment]::GetEnvironmentVariable('Path','Machine')
  $user=[Environment]::GetEnvironmentVariable('Path','User')
  $env:Path=((@($machine,$user) | Where-Object { $_ }) -join ';')
}
if(-not (Have winget)){ throw "Windows App Installer (winget) is not available. Install 'App Installer' from the Microsoft Store, then run install.bat again. If you only want to use GraphVis, download the Windows release ZIP that contains Install.exe instead of this source package." }
foreach($tool in @(@('Git.Git','git'),@('Kitware.CMake','cmake'),@('Ninja-build.Ninja','ninja'))){
  if(-not (Have $tool[1])){ winget install --id $tool[0] -e --silent --accept-package-agreements --accept-source-agreements; if($LASTEXITCODE -ne 0){throw "Failed to install $($tool[0])"}; Refresh-ProcessPath }
}
if(-not (Have cargo)){
  winget install --id Rustlang.Rustup -e --silent --accept-package-agreements --accept-source-agreements
  if($LASTEXITCODE -ne 0){throw 'Failed to install Rustup'}
  Refresh-ProcessPath
  $cargoBin=Join-Path $HOME '.cargo\bin'
  if($env:Path -notlike "*$cargoBin*"){$env:Path += ';'+$cargoBin}
}
if(Have rustup){ rustup toolchain install 1.98.0 --profile minimal --component rustfmt --component clippy; if($LASTEXITCODE -ne 0){throw 'Pinned Rust toolchain installation failed'} }

$Tooling=Join-Path $Root '.tooling'; New-Item $Tooling -ItemType Directory -Force | Out-Null
# The pinned vcpkg commit is NOT written here. It is vcpkg.json's
# "builtin-baseline", and it has to be, because vcpkg resolves each dependency
# to the version recorded at the baseline commit and then looks that version up
# in the CHECKED-OUT version database. If the two disagree every package fails
# with "no version database entry for <pkg> at <version>", which names the
# package and tells you nothing about the real cause.
#
# They did disagree: the baseline was 30ef65cad9 (31 Aug 2026) while five
# different files each hardcoded 9e593bb18e (29 Jul 2026), a month behind. It
# only surfaced when the vcpkg tree was cleaned and re-checked-out, because
# until then an already-resolved tree was being reused.
$Vcpkg=Join-Path $Tooling 'vcpkg'
$VcpkgCommit=(Get-Content (Join-Path $Root 'vcpkg.json') -Raw | ConvertFrom-Json).'builtin-baseline'
if(-not $VcpkgCommit){throw 'vcpkg.json has no builtin-baseline; cannot pin vcpkg.'}
if(-not (Test-Path (Join-Path $Vcpkg '.git'))){ 
  git clone https://github.com/microsoft/vcpkg.git $Vcpkg
  if($LASTEXITCODE -ne 0){throw 'vcpkg clone failed'} 
}
pushd $Vcpkg
try {
  git fetch origin
  git checkout -f $VcpkgCommit
  if($LASTEXITCODE -ne 0){throw 'vcpkg pinned commit checkout failed'}
  if(-not (Test-Path (Join-Path $Vcpkg 'vcpkg.exe'))){ & (Join-Path $Vcpkg 'bootstrap-vcpkg.bat') -disableMetrics; if($LASTEXITCODE -ne 0){throw 'vcpkg bootstrap failed'} }
} finally { popd }
$env:VCPKG_ROOT=$Vcpkg

if(-not (Have sccache)){
  $SccacheVersion='0.16.0'; $SccacheSha='b8514ed7552e148b0a032114f745118dcb801791adafafeaf9935e4bfb0edf1b'
  $SccacheDir=Join-Path $Tooling 'sccache'; New-Item -ItemType Directory -Force $SccacheDir | Out-Null
  $Zip=Join-Path $SccacheDir 'sccache.zip'
  Invoke-WebRequest -Uri "https://github.com/mozilla/sccache/releases/download/v$SccacheVersion/sccache-v$SccacheVersion-x86_64-pc-windows-msvc.zip" -OutFile $Zip
  if((Get-FileHash $Zip -Algorithm SHA256).Hash.ToLower() -ne $SccacheSha){throw 'sccache archive checksum mismatch'}
  Expand-Archive $Zip $SccacheDir -Force
  $SccacheExe=Get-ChildItem $SccacheDir -Recurse -Filter sccache.exe | Select-Object -First 1
$TargetExe=Join-Path $SccacheDir 'sccache.exe'
if($SccacheExe.FullName -ne $TargetExe){
  Copy-Item $SccacheExe.FullName $TargetExe -Force
}
$env:Path="$SccacheDir;$env:Path"}
Initialize-GraphVisBuildEnvironment -Root $Root
if(-not (Test-Path (Join-Path $Root 'native\Cargo.lock'))){
  Write-Warning 'native/Cargo.lock is missing. Generating it once with the pinned toolchain. Commit it before release CI.'
  cargo generate-lockfile --manifest-path (Join-Path $Root 'native\Cargo.toml')
  if($LASTEXITCODE -ne 0){throw 'Cargo.lock generation failed'}
}
Assert-GraphVisLockfiles -Root $Root

Write-Host 'Prewarming pinned native dependencies...' -ForegroundColor Cyan
& (Join-Path $Vcpkg 'vcpkg.exe') install --triplet x64-windows --x-manifest-root=$Root
if($LASTEXITCODE -ne 0){throw 'vcpkg dependency prewarm failed'}
cargo fetch --locked --manifest-path (Join-Path $Root 'native\Cargo.toml')
if($LASTEXITCODE -ne 0){throw 'Cargo dependency prewarm failed'}

if($SetupOnly){ Write-Host 'Developer toolchain setup complete.' -ForegroundColor Green; exit 0 }
if(-not $SkipInstaller -and -not (Have makensis)){
  winget install --id NSIS.NSIS -e --silent --accept-package-agreements --accept-source-agreements
  if($LASTEXITCODE -ne 0){throw 'NSIS installation failed'}
  Refresh-ProcessPath
}
& (Join-Path $PSScriptRoot 'Build-Release.ps1') -VcpkgRoot $Vcpkg -SkipInstaller:$SkipInstaller -Fast:$Fast
