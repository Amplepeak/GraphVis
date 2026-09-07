# Watch-Build - leave this running and builds happen by themselves.
#
# Polls the GraphVis sources, waits for a burst of edits to settle, then runs
# the same pipeline BUILD-AND-DIAGNOSE.bat runs and writes full-log.txt.
#
# Three things it now does that it did not:
#
#   It always builds once on start. It used to record a marker at launch and
#   ignore anything older, so every edit made while it was closed was invisible
#   to it - the build looked like it ran and quietly did nothing.
#
#   It reports the outcome in the window. A green OK line, or the first errors
#   printed inline in red, so a failure does not require opening a 170 KB log to
#   discover. The window is useful minimised on a second screen because the last
#   line always says where things stand.
#
#   It says what triggered the build, so an unexpected rebuild is explicable.
#
# Stop it with Ctrl+C, or close the window.
param(
  [int]$PollSeconds = 5,
  [int]$SettleSeconds = 3,
  [switch]$RunOnce,
  # Skip the build-on-start. Only useful if you know the tree is already built.
  [switch]$NoInitialBuild
)
$ErrorActionPreference = 'Continue'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Log  = Join-Path $Root 'full-log.txt'
$Marker = Join-Path $Root '.cache\last-watch-build.txt'
New-Item -ItemType Directory -Force -Path (Split-Path $Marker) | Out-Null

# Only real inputs. Build outputs, caches and the vendored toolchain are
# excluded, or the watcher would retrigger on its own output forever.
$WatchDirs = @('app', 'native', 'services', 'config', 'tools', 'scripts', 'packaging', 'installer') |
             ForEach-Object { Join-Path $Root $_ } | Where-Object { Test-Path $_ }
# .ps1 and .bat are watched too. They were not, so an edit to a build script
# never triggered anything - which is part of why two scripts that could not
# even be parsed sat in the tree for months. A script change rebuilds nothing
# (ninja has no work to do) but it does re-run the parse check in Full-Diagnose,
# which is the whole point.
$Extensions = @('*.qml', '*.cpp', '*.h', '*.rs', '*.py', '*.json', '*.manifest',
                'CMakeLists.txt', '*.cmake', '*.ps1', '*.bat', '*.nsi')

function Get-SourceState {
  # Returns the newest write time AND the file it belongs to, so the watcher can
  # say what woke it rather than just that something did.
  $latest = [datetime]'2000-01-01'
  $who = ''
  foreach ($dir in $WatchDirs) {
    foreach ($ext in $Extensions) {
      Get-ChildItem $dir -Filter $ext -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '\\(__pycache__|\.cache|build|\.tooling)\\' } |
        ForEach-Object {
          if ($_.LastWriteTime -gt $latest) { $latest = $_.LastWriteTime; $who = $_.FullName }
        }
    }
  }
  foreach ($f in @('CMakeLists.txt','CMakePresets.json','vcpkg.json')) {
    $p = Join-Path $Root $f
    if (Test-Path $p) {
      $item = Get-Item $p
      if ($item.LastWriteTime -gt $latest) { $latest = $item.LastWriteTime; $who = $item.FullName }
    }
  }
  if ($who) { $who = $who.Substring($Root.Length).TrimStart('\') }
  return [pscustomobject]@{ Time = $latest; File = $who }
}

function Invoke-Build {
  param([string]$Because)
  $started = Get-Date
  if ($Because) {
    Write-Host ("[{0}] building - {1}" -f $started.ToString('HH:mm:ss'), $Because) -ForegroundColor Cyan
  } else {
    Write-Host ("[{0}] building..." -f $started.ToString('HH:mm:ss')) -ForegroundColor Cyan
  }

  # `*> file` writes UTF-16LE in Windows PowerShell, which makes the log
  # unreadable to ordinary text tooling. Force UTF-8.
  & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'Full-Diagnose.ps1') 2>&1 |
    Out-File -FilePath $Log -Encoding utf8
  Add-Content -Path $Log -Value 'DONE' -Encoding utf8

  $elapsed = [int]((Get-Date) - $started).TotalSeconds
  $text = Get-Content $Log -Raw -ErrorAction SilentlyContinue
  if (-not $text) { $text = '' }

  # Compiler and linker errors first: those are what stop everything else.
  $errors = Select-String -Path $Log -Pattern 'error C\d+|error LNK|^FAILED:|CMake Error' -ErrorAction SilentlyContinue |
            ForEach-Object { $_.Line.Trim() } | Select-Object -First 6
  $qml = Select-String -Path $Log -Pattern 'QML: .*(TypeError|ReferenceError|is not a type|Cannot|not installed)' -ErrorAction SilentlyContinue |
         ForEach-Object { $_.Line.Trim() } | Select-Object -Unique | Select-Object -First 4
  $opened = $text -match 'OPENED ITS WINDOW'
  $selftest = $text -match 'VECTOR PDF OK'

  Write-Host ""
  if ($errors) {
    Write-Host ("[{0}] BUILD FAILED after {1}s" -f (Get-Date -Format 'HH:mm:ss'), $elapsed) -ForegroundColor Red
    foreach ($line in $errors) { Write-Host "    $line" -ForegroundColor Red }
    Write-Host "    full log: $Log" -ForegroundColor DarkGray
  }
  elseif ($qml) {
    Write-Host ("[{0}] built in {1}s, but QML reported problems" -f (Get-Date -Format 'HH:mm:ss'), $elapsed) -ForegroundColor Yellow
    foreach ($line in $qml) { Write-Host "    $line" -ForegroundColor Yellow }
  }
  elseif ($opened) {
    $extra = if ($selftest) { 'window opened, vector PDF OK' } else { 'window opened' }
    Write-Host ("[{0}] OK in {1}s - {2}" -f (Get-Date -Format 'HH:mm:ss'), $elapsed, $extra) -ForegroundColor Green
  }
  else {
    Write-Host ("[{0}] built in {1}s, but the app did not reach its window" -f (Get-Date -Format 'HH:mm:ss'), $elapsed) -ForegroundColor Yellow
    Write-Host "    full log: $Log" -ForegroundColor DarkGray
  }
  Write-Host ""
  return -not [bool]$errors
}

Write-Host "GraphVis build watcher" -ForegroundColor Green
Write-Host "Watching: $($WatchDirs -join ', ')"
Write-Host "Log:      $Log"
Write-Host "Safe to minimise - the last line always says where things stand."
Write-Host "Press Ctrl+C to stop. You can keep using your computer; this only builds.`n"

$state = Get-SourceState
if ($RunOnce) { Invoke-Build | Out-Null; Set-Content $Marker $state.Time.ToString('o'); return }

# Build once on start, always. Whatever changed while this was closed is
# exactly what the watcher used to throw away.
if (-not $NoInitialBuild) {
  Invoke-Build -Because 'first build since the watcher started' | Out-Null
}
$lastBuilt = $state.Time
Set-Content $Marker $lastBuilt.ToString('o')

$idleSince = Get-Date
while ($true) {
  try {
    $state = Get-SourceState
    if ($state.Time -gt $lastBuilt) {
      # Wait for a burst of edits to finish before starting.
      do {
        Start-Sleep -Seconds $SettleSeconds
        $again = Get-SourceState
        $settled = ($again.Time -eq $state.Time)
        $state = $again
      } until ($settled)

      # Record the source timestamp that was actually built, NOT the moment the
      # build finished. Using the finish time silently discards any edit made
      # while the build was running: it is older than the finish time, so the
      # next poll considers it already built and never rebuilds it.
      $builtUpTo = $state.Time
      Invoke-Build -Because $state.File | Out-Null
      $lastBuilt = $builtUpTo
      Set-Content $Marker $lastBuilt.ToString('o')
      $idleSince = Get-Date
    }
    elseif (((Get-Date) - $idleSince).TotalMinutes -ge 15) {
      # Proof of life for a window that has been minimised for an hour.
      Write-Host ("[{0}] idle - watching, nothing has changed" -f (Get-Date -Format 'HH:mm:ss')) -ForegroundColor DarkGray
      $idleSince = Get-Date
    }
  } catch {
    Write-Host "[watcher] $($_.Exception.Message)" -ForegroundColor Yellow
  }
  Start-Sleep -Seconds $PollSeconds
}
