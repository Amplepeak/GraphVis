<#
Reclaims disk space from build and package caches.

Run with no switches it only measures and reports - nothing is deleted until
you say which tier you want. That is deliberate: two of the folders in here
look like junk and are not, and deleting them costs hours rather than minutes.

  -Intermediates   vcpkg buildtrees, packages and downloads.
                   The big win, and the cheap one. These are scratch space from
                   compiling the dependencies; vcpkg only needs them again if it
                   rebuilds a port from source, and the binary cache means it
                   usually does not have to.

  -BuildTree       build/. Every compiled object and the staged application.
                   Costs one full rebuild - roughly 25 minutes, most of it the
                   Rust core - and nothing else.

  -All             Both of the above.

What this will NOT delete, at any setting:

  .cache/vcpkg-binaries   The binary cache. This is what makes a rebuild take
                          minutes instead of the several hours it took to
                          compile Qt, VTK, Arrow and Boost the first time.
                          Deleting it is the single most expensive mistake
                          available in this project.
  .tooling/vcpkg          The vcpkg tool, its ports and its version database.
                          Without it the project cannot configure at all.
  .cache/sccache          The C++ compiler cache. Small, and saves real time.
#>
param(
  [switch]$Intermediates,
  [switch]$BuildTree,
  [switch]$All
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ($All) { $Intermediates = $true; $BuildTree = $true }

function Get-FolderSize {
  param([string]$Path)
  if (-not (Test-Path $Path)) { return $null }
  try {
    $bytes = (Get-ChildItem $Path -Recurse -File -Force -ErrorAction SilentlyContinue |
              Measure-Object -Property Length -Sum).Sum
    return [math]::Round(($bytes / 1GB), 2)
  } catch { return $null }
}

# rd is dramatically faster than Remove-Item for trees of this size - it is a
# single call into the shell rather than a pipeline object per file.
function Remove-Tree {
  param([string]$Path, [string]$Label)
  if (-not (Test-Path $Path)) { Write-Host "  $Label - already gone"; return }
  $size = Get-FolderSize $Path
  Write-Host ("  removing {0} ({1} GB)..." -f $Label, $size) -NoNewline
  & cmd.exe /c "rd /s /q `"$Path`"" 2>$null
  if (Test-Path $Path) {
    Write-Host " partially removed (something is holding a file open)" -ForegroundColor Yellow
  } else {
    Write-Host " done" -ForegroundColor Green
  }
  return $size
}

$targets = [ordered]@{
  'vcpkg buildtrees' = Join-Path $Root '.tooling\vcpkg\buildtrees'
  'vcpkg packages'   = Join-Path $Root '.tooling\vcpkg\packages'
  'vcpkg downloads'  = Join-Path $Root '.tooling\vcpkg\downloads'
  'build tree'       = Join-Path $Root 'build'
}
$keep = [ordered]@{
  'vcpkg binary cache' = Join-Path $Root '.cache\vcpkg-binaries'
  'vcpkg tool + ports' = Join-Path $Root '.tooling\vcpkg'
  'rust target cache'  = Join-Path $Root '.cache\cargo-target'
  'sccache'            = Join-Path $Root '.cache\sccache'
}

Write-Host "GraphVis disk usage" -ForegroundColor Cyan
Write-Host "  (measuring - this walks a lot of files, give it a minute)" -ForegroundColor DarkGray
Write-Host ""
Write-Host "Reclaimable:" -ForegroundColor Cyan
$reclaimable = 0
foreach ($name in $targets.Keys) {
  $size = Get-FolderSize $targets[$name]
  if ($null -eq $size) { Write-Host ("  {0,-20} not present" -f $name); continue }
  Write-Host ("  {0,-20} {1,8} GB" -f $name, $size)
  $reclaimable += $size
}
Write-Host ("  {0,-20} {1,8} GB total" -f '', $reclaimable) -ForegroundColor Green
Write-Host ""
Write-Host "Kept whatever you choose - deleting these costs hours, not minutes:" -ForegroundColor Cyan
foreach ($name in $keep.Keys) {
  $size = Get-FolderSize $keep[$name]
  if ($null -eq $size) { continue }
  Write-Host ("  {0,-20} {1,8} GB" -f $name, $size) -ForegroundColor DarkGray
}
Write-Host ""

# Run with no switches, this asks rather than just reporting and stopping.
# Printing what could be deleted and then doing nothing reads as a failure, and
# the switches are only worth having for an unattended run.
if (-not $Intermediates -and -not $BuildTree) {
  Write-Host "Delete the vcpkg intermediates? Safe - your next build is unaffected." -ForegroundColor Yellow
  $answer = Read-Host "  [Y] yes  [N] no  (default Y)"
  if ($answer -eq '' -or $answer -match '^[Yy]') { $Intermediates = $true }

  Write-Host ""
  Write-Host "Also delete the build tree? This costs one full rebuild, about 25 minutes." -ForegroundColor Yellow
  $answer = Read-Host "  [Y] yes  [N] no  (default N)"
  if ($answer -match '^[Yy]') { $BuildTree = $true }

  if (-not $Intermediates -and -not $BuildTree) {
    Write-Host ""
    Write-Host "Nothing deleted." -ForegroundColor Cyan
    return
  }
  Write-Host ""
}

$freed = 0
if ($Intermediates) {
  Write-Host "Removing vcpkg intermediates" -ForegroundColor Cyan
  $freed += [double](Remove-Tree $targets['vcpkg buildtrees'] 'vcpkg buildtrees')
  $freed += [double](Remove-Tree $targets['vcpkg packages']   'vcpkg packages')
  $freed += [double](Remove-Tree $targets['vcpkg downloads']  'vcpkg downloads')
}
if ($BuildTree) {
  Write-Host "Removing the build tree" -ForegroundColor Cyan
  Write-Host "  Stop WATCH-BUILD.bat first if it is running." -ForegroundColor Yellow
  $freed += [double](Remove-Tree $targets['build tree'] 'build')
}

Write-Host ""
Write-Host ("Freed roughly {0} GB." -f [math]::Round($freed, 2)) -ForegroundColor Green
if ($BuildTree) {
  Write-Host "The next build is a full one - about 25 minutes, mostly the Rust core." -ForegroundColor Yellow
} else {
  Write-Host "Your next build is unaffected." -ForegroundColor Green
}
