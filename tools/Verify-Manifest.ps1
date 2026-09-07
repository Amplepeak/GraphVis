param([switch]$Full)
$ErrorActionPreference = 'Continue'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Build = if ($Full) { Join-Path $Root 'build/windows-release' } else { Join-Path $Root 'build/windows-fast' }

Write-Host "=== 1. Building ===" -ForegroundColor Cyan
try {
  if ($Full) { & (Join-Path $PSScriptRoot 'Quick-Build.ps1') -Full }
  else       { & (Join-Path $PSScriptRoot 'Quick-Build.ps1') }
  Write-Host "Quick-Build finished (exit code $LASTEXITCODE)"
} catch {
  Write-Host "[BUILD ERROR] $($_.Exception.Message)" -ForegroundColor Red
  Write-Host $_.ScriptStackTrace
}

$exe = Join-Path $Build 'graphvis.exe'
if (-not (Test-Path $exe)) { Write-Host "[FAIL] $exe was not produced." -ForegroundColor Red; exit 1 }
Write-Host "Built: $exe ($((Get-Item $exe).LastWriteTime))"

Write-Host "`n=== 2. Embedded manifest ===" -ForegroundColor Cyan
$mt = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin\*\x64\mt.exe' -ErrorAction SilentlyContinue |
      Sort-Object FullName | Select-Object -Last 1
$out = Join-Path $Root 'extracted.manifest'
Remove-Item $out -Force -ErrorAction SilentlyContinue
if ($mt) {
  & $mt.FullName -nologo "-inputresource:$exe;#1" "-out:$out" 2>&1 | Write-Host
}
if (Test-Path $out) {
  Get-Content $out | Write-Host
  $txt = Get-Content $out -Raw
  if ($txt -match 'ms_asmv2:') {
    Write-Host "`n[FAIL] Manifest still contains ms_asmv2: prefixed attributes." -ForegroundColor Red
  } elseif ($txt -match '<requestedExecutionLevel\s+level="asInvoker"') {
    Write-Host "`n[OK] requestedExecutionLevel has an unprefixed level attribute." -ForegroundColor Green
  } else {
    Write-Host "`n[WARN] Could not find a plain requestedExecutionLevel line." -ForegroundColor Yellow
  }
} else {
  Write-Host "[INFO] No manifest resource extracted (mt.exe not found, or no manifest embedded)."
}

Write-Host "`n=== 3. Launch test ===" -ForegroundColor Cyan
$p = Start-Process -FilePath $exe -WorkingDirectory $Build -PassThru -ErrorAction SilentlyContinue
if (-not $p) { Write-Host "[FAIL] Could not start graphvis.exe." -ForegroundColor Red; exit 1 }
Start-Sleep -Seconds 8
if ($p.HasExited) {
  Write-Host ("[FAIL] graphvis.exe exited after {0}s with code 0x{1:X8}" -f 8, $p.ExitCode) -ForegroundColor Red
} else {
  Write-Host "[OK] graphvis.exe is still running after 8s - the SxS failure is gone." -ForegroundColor Green
  Write-Host "Leaving it open. Close the window when you are done looking at it."
}
