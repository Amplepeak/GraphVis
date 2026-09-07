<#
Install, remove and report the optional GraphVis components.

One script for three callers - INSTALL-DATA-FORMATS.bat at install time, the
Add-ons panel inside the application, and a person at a prompt - so what
"installed" means cannot drift between them. The component list itself lives in
tools/optional-components.json and is never duplicated here.

  -List                    JSON to stdout: every component with its installed state
  -Install io_extra,vlm    install those extras into the science environment
  -Remove vlm              uninstall that extra's packages
#>
param(
  [switch]$List,
  [string]$Install = '',
  [string]$Remove = ''
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$EnvDir = Join-Path $env:LOCALAPPDATA 'GraphVis\18.4\python-science'
$Python = Join-Path $EnvDir 'Scripts\python.exe'
$Catalogue = Join-Path $PSScriptRoot 'optional-components.json'

if (-not (Test-Path $Catalogue)) { throw "Component list not found: $Catalogue" }
$components = (Get-Content $Catalogue -Raw | ConvertFrom-Json).components

function Test-Component($probe) {
  # Importability is the only honest test. A package can be listed in pip's
  # metadata and still fail to import - a broken wheel, a missing DLL - and an
  # Add-ons panel that says "installed" about one of those is worse than useless.
  if (-not (Test-Path $Python)) { return $false }
  & $Python -c "import $probe" 2>$null | Out-Null
  return ($LASTEXITCODE -eq 0)
}

function Ensure-Environment {
  if (Test-Path $Python) { return }
  if (-not (Get-Command py.exe -ErrorAction SilentlyContinue)) {
    throw 'Python 3.12 is needed for the science components. Install it and run this again.'
  }
  & py -3.12 -m venv $EnvDir
  & $Python -m pip install --upgrade pip
}

if ($List) {
  $out = foreach ($c in $components) {
    [pscustomobject]@{
      key       = $c.key
      name      = $c.name
      summary   = $c.summary
      cost      = $c.cost
      sizeMb    = $c.size_mb
      default   = [bool]$c.default
      installed = (Test-Component $c.probe)
    }
  }
  # -Depth so the array is emitted as objects rather than type names, and
  # -Compact because the caller parses this, it does not read it.
  ConvertTo-Json @($out) -Depth 4 -Compress
  exit 0
}

if ($Install) {
  Ensure-Environment
  $wanted = $Install.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ }
  $failed = @()
  foreach ($key in $wanted) {
    $component = $components | Where-Object { $_.key -eq $key }
    if (-not $component) { Write-Host "Unknown component '$key' - skipped" -ForegroundColor Yellow; continue }
    # One extra at a time. A combined install that fails takes every other
    # component down with it, which is how one unavailable wheel used to cost
    # a user every format they asked for.
    $target = (Join-Path $Root 'services\python') + "[$($component.extra)]"
    Write-Host "Installing $($component.name) (~$($component.size_mb) MB)" -ForegroundColor Cyan
    & $Python -m pip install $target
    if ($LASTEXITCODE -ne 0) { $failed += $component.name }
  }
  if ($failed.Count -gt 0) {
    Write-Host ("Could not install: {0}" -f ($failed -join ', ')) -ForegroundColor Yellow
    Write-Host 'Everything else is installed and usable.' -ForegroundColor Yellow
    exit 2
  }
  exit 0
}

if ($Remove) {
  if (-not (Test-Path $Python)) { throw 'The science environment is not installed, so there is nothing to remove.' }
  $reqScript = Join-Path $PSScriptRoot 'component_requirements.py'
  $wanted = $Remove.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ }
  foreach ($key in $wanted) {
    $component = $components | Where-Object { $_.key -eq $key }
    if (-not $component) { Write-Host "Unknown component '$key' - skipped" -ForegroundColor Yellow; continue }

    # What this extra installed, read out of the package metadata rather than
    # kept as a second copy here that would go stale the first time
    # pyproject.toml changed.
    $names = & $Python $reqScript $component.extra
    if ($LASTEXITCODE -ne 0 -or -not $names) {
      Write-Host "Could not work out what '$key' installed, so nothing was removed." -ForegroundColor Yellow
      continue
    }
    $drop = @($names | ForEach-Object { $_.Trim() } | Where-Object { $_ })

    # A package shared with a component that is staying must not be pulled out
    # from under it. h5py belongs to two groups; removing one would break both.
    foreach ($other in $components) {
      if ($other.key -eq $key) { continue }
      if (-not (Test-Component $other.probe)) { continue }
      $keep = @(& $Python $reqScript $other.extra | ForEach-Object { $_.Trim() })
      $drop = @($drop | Where-Object { $keep -notcontains $_ })
    }

    if ($drop.Count -eq 0) {
      Write-Host "Everything '$key' installed is shared with a component you are keeping." -ForegroundColor Yellow
      continue
    }
    Write-Host ("Removing {0}: {1}" -f $component.name, ($drop -join ', ')) -ForegroundColor Cyan
    & $Python -m pip uninstall -y @drop
  }
  exit 0
}

Write-Host 'Nothing to do. Pass -List, -Install <keys> or -Remove <keys>.'
