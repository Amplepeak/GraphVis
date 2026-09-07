Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:GraphVisVersion = '18.4.0'
$script:GraphVisStateRoot = Join-Path $env:LOCALAPPDATA 'GraphVis\18.4'
$script:GraphVisStateFile = Join-Path $script:GraphVisStateRoot '.setup_complete'
$script:GraphVisRegistry = 'HKCU:\Software\GraphVis\18.4'

function Get-GraphVisInstallDir {
    if (Test-Path $script:GraphVisRegistry) {
        try {
            $dir = (Get-ItemProperty -Path $script:GraphVisRegistry -Name InstallDir -ErrorAction Stop).InstallDir
            if ($dir) { return [string]$dir }
        } catch {}
    }
    return (Join-Path $env:LOCALAPPDATA 'Programs\GraphVis 18.4')
}

function Get-GraphVisCriticalPaths([string]$InstallDir) {
    @(
        (Join-Path $InstallDir 'graphvis.exe'),
        (Join-Path $InstallDir 'graphvis_ffi.dll'),
        (Join-Path $InstallDir 'qml\GraphVis\VTK\qmldir'),
        (Join-Path $InstallDir 'runtime-manifest.sha256')
    )
}

function Test-GraphVisRuntime([string]$InstallDir, [switch]$VerifyManifest) {
    $errors = [System.Collections.Generic.List[string]]::new()
    foreach ($path in Get-GraphVisCriticalPaths $InstallDir) {
        if (-not (Test-Path -LiteralPath $path)) { $errors.Add("Missing required runtime file: $path") }
    }

    $vtkDir = Join-Path $InstallDir 'qml\GraphVis\VTK'
    if (Test-Path $vtkDir) {
        $plugin = Get-ChildItem -LiteralPath $vtkDir -Filter '*Plugin*.dll' -File -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $plugin) { $errors.Add("Missing VTK QML plugin DLL in: $vtkDir") }
    }

    if ($VerifyManifest) {
        $manifest = Join-Path $InstallDir 'runtime-manifest.sha256'
        if (Test-Path $manifest) {
            foreach ($line in Get-Content -LiteralPath $manifest) {
                if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith('#')) { continue }
                $parts = $line -split '\s{2}', 2
                if ($parts.Count -ne 2) { $errors.Add("Malformed manifest line: $line"); continue }
                $expected = $parts[0].Trim().ToLowerInvariant()
                $relative = $parts[1].Trim().Replace('/', '\')
                $file = Join-Path $InstallDir $relative
                if (-not (Test-Path -LiteralPath $file)) { $errors.Add("Manifest file missing: $relative"); continue }
                $actual = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
                if ($actual -ne $expected) { $errors.Add("Checksum mismatch: $relative") }
            }
        }
    }

    [pscustomobject]@{ Ok = ($errors.Count -eq 0); Errors = @($errors) }
}

function Set-UserEnvironmentValue([string]$Name, [string]$Value) {
    [Environment]::SetEnvironmentVariable($Name, $Value, 'User')
}

function Write-GraphVisInstallState([string]$InstallDir) {
    $check = Test-GraphVisRuntime -InstallDir $InstallDir -VerifyManifest
    if (-not $check.Ok) { throw "Runtime verification failed; setup marker will NOT be written.`n - " + ($check.Errors -join "`n - ") }
    New-Item -ItemType Directory -Force -Path $script:GraphVisStateRoot | Out-Null
    New-Item -ItemType Directory -Force -Path $script:GraphVisRegistry | Out-Null
    New-ItemProperty -Path $script:GraphVisRegistry -Name InstallDir -Value $InstallDir -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $script:GraphVisRegistry -Name Version -Value $script:GraphVisVersion -PropertyType String -Force | Out-Null
    Set-UserEnvironmentValue 'GRAPHVIS_HOME' $InstallDir

    $state = [ordered]@{
        schema = 2
        product = 'GraphVis'
        version = $script:GraphVisVersion
        install_dir = $InstallDir
        verified_utc = [DateTime]::UtcNow.ToString('o')
    }
    $state | ConvertTo-Json | Set-Content -LiteralPath $script:GraphVisStateFile -Encoding UTF8
    return $script:GraphVisStateFile
}

function Test-GraphVisInstallState {
    $dir = Get-GraphVisInstallDir
    if (-not (Test-Path -LiteralPath $script:GraphVisStateFile)) {
        return [pscustomobject]@{ Ok=$false; InstallDir=$dir; Reason="Setup state marker is missing: $script:GraphVisStateFile" }
    }
    try { $state = Get-Content -LiteralPath $script:GraphVisStateFile -Raw | ConvertFrom-Json }
    catch { return [pscustomobject]@{ Ok=$false; InstallDir=$dir; Reason='Setup state marker is unreadable.' } }
    if ($state.version -ne $script:GraphVisVersion) { return [pscustomobject]@{ Ok=$false; InstallDir=$dir; Reason="Setup state version mismatch: $($state.version)" } }
    if ($state.install_dir -and (Test-Path -LiteralPath $state.install_dir)) { $dir = [string]$state.install_dir }
    $runtime = Test-GraphVisRuntime -InstallDir $dir
    if (-not $runtime.Ok) { return [pscustomobject]@{ Ok=$false; InstallDir=$dir; Reason=($runtime.Errors -join '; ') } }
    [pscustomobject]@{ Ok=$true; InstallDir=$dir; Reason='OK' }
}

function Remove-GraphVisInstallState([string]$InstallDir) {
    [Environment]::SetEnvironmentVariable('GRAPHVIS_HOME', $null, 'User')
    Remove-Item -LiteralPath $script:GraphVisStateFile -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $script:GraphVisRegistry -Recurse -Force -ErrorAction SilentlyContinue
}
