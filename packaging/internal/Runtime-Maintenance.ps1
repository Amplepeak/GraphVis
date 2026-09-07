param(
    [Parameter(Mandatory=$true)][ValidateSet('Install','Repair','Verify','Uninstall')][string]$Mode,
    [Parameter(Mandatory=$true)][string]$InstallDir,
    [string]$LogPath
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $Root 'Install-State.ps1')
if (-not $LogPath) {
    $logDir = Join-Path $env:LOCALAPPDATA 'GraphVis\18.4\logs'
    New-Item -ItemType Directory -Force -Path $logDir | Out-Null
    $LogPath = Join-Path $logDir 'maintenance.log'
}
function Log($m){ "$(Get-Date -Format o) $m" | Add-Content -LiteralPath $LogPath -Encoding UTF8 }
try {
    Log "Mode=$Mode InstallDir=$InstallDir"
    switch ($Mode) {
        'Install' {
            $state = Write-GraphVisInstallState -InstallDir $InstallDir
            Log "Verified install and wrote state: $state"
        }
        'Repair' {
            $state = Write-GraphVisInstallState -InstallDir $InstallDir
            Log "Repair verification succeeded and state was refreshed: $state"
        }
        'Verify' {
            $check = Test-GraphVisRuntime -InstallDir $InstallDir -VerifyManifest
            if (-not $check.Ok) { throw ($check.Errors -join "`n") }
            Log 'Full runtime manifest verification succeeded.'
        }
        'Uninstall' {
            Remove-GraphVisInstallState -InstallDir $InstallDir
            Log 'Removed GraphVis-owned environment/state entries.'
        }
    }
    exit 0
} catch {
    Log "ERROR: $($_.Exception.Message)"
    Write-Error $_
    exit 1
}
