$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'Dev-State.ps1')
$check=Test-GraphVisDevState -Root $Root
if(-not $check.Ok){throw "Developer bootstrap is incomplete or stale:`n - "+($check.Errors -join "`n - ")+"`nRun install.bat once."}
$env:VCPKG_ROOT=Join-Path $Root '.tooling\vcpkg'

# ---------------------------------------------------------------------------
# Find makensis, or install NSIS and find it then.
#
# This used to install NSIS and throw on a non-zero winget exit code. That is
# wrong, and it stopped a release build that had everything it needed: when the
# package is ALREADY installed winget prints "No available upgrade found" and
# exits non-zero (0x8A15002B), so a machine with NSIS installed but not yet on
# PATH - which is exactly the machine that reaches this block - failed here
# every time.
#
# The exit code is not the question. The question is whether makensis.exe can
# be run afterwards, so that is what is now tested, and it is tested by looking
# in the places NSIS actually installs to rather than by hoping PATH was
# refreshed.
# ---------------------------------------------------------------------------
function Find-MakeNsis {
  $cmd=Get-Command makensis -ErrorAction SilentlyContinue
  if($cmd){ return $cmd.Source }
  # A fresh install writes to the machine PATH, which this process inherited at
  # start-up and will not see until it is re-read.
  $machine=[Environment]::GetEnvironmentVariable('Path','Machine')
  $user=[Environment]::GetEnvironmentVariable('Path','User')
  $env:Path=((@($machine,$user)|Where-Object{$_}) -join ';')
  $cmd=Get-Command makensis -ErrorAction SilentlyContinue
  if($cmd){ return $cmd.Source }
  # The install directory, from the registry NSIS writes and from the two
  # standard locations. winget's shim directory is included because a winget
  # install can put the shim there and nothing else on PATH.
  $candidates=@()
  foreach($key in @('HKLM:\SOFTWARE\WOW6432Node\NSIS','HKLM:\SOFTWARE\NSIS')){
    try{
      $dir=(Get-ItemProperty -Path $key -ErrorAction Stop).'(default)'
      if($dir){ $candidates+=$dir }
    }catch{}
  }
  $candidates+=@(
    'C:\Program Files (x86)\NSIS',
    'C:\Program Files\NSIS',
    (Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Links'),
    (Join-Path $env:ProgramData 'chocolatey\bin')
  )
  foreach($dir in $candidates){
    if(-not $dir){ continue }
    $exe=Join-Path $dir 'makensis.exe'
    if(Test-Path $exe){ $env:Path="$dir;$env:Path"; return $exe }
  }
  return $null
}

$makensis=Find-MakeNsis
if(-not $makensis){
  if(-not (Get-Command winget -ErrorAction SilentlyContinue)){
    throw 'NSIS/makensis is missing and winget is unavailable. Install the free NSIS package (NSIS.NSIS) and run this again.'
  }
  Write-Host 'Installing the free NSIS compiler for release packaging...' -ForegroundColor Cyan
  # The exit code is recorded and reported, never thrown on: "already installed"
  # and "no upgrade available" are both non-zero and both mean the machine is
  # fine. Only the search below decides.
  winget install --id NSIS.NSIS -e --silent --accept-package-agreements --accept-source-agreements
  $wingetCode=$LASTEXITCODE
  $makensis=Find-MakeNsis
  if(-not $makensis){
    throw ("makensis.exe is still unavailable after NSIS setup (winget exit code $wingetCode).`n"+
           "Install NSIS from https://nsis.sourceforge.io/Download, or run:`n"+
           "    winget install --id NSIS.NSIS -e`n"+
           "then re-run build.bat. It is looked for on PATH, in the NSIS registry key, "+
           "in 'C:\Program Files (x86)\NSIS' and in 'C:\Program Files\NSIS'.")
  }
}
Write-Host "Using NSIS at $makensis" -ForegroundColor DarkGray

& (Join-Path $PSScriptRoot 'Build-Release.ps1')
