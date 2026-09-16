Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$script:DevStateVersion=1
$script:RustVersion='1.98.0'
# Read from vcpkg.json rather than restated here - see Bootstrap-WindowsBuild.ps1.
function Get-GraphVisVcpkgBaseline([string]$Root){
  (Get-Content (Join-Path $Root 'vcpkg.json') -Raw | ConvertFrom-Json).'builtin-baseline'
}
function Get-DevStatePath([string]$Root){ Join-Path $Root '.tooling\dev-state.json' }
function Test-Cmd([string]$n){ [bool](Get-Command $n -ErrorAction SilentlyContinue) }
function Test-GraphVisDevState([string]$Root){
  $errors=[System.Collections.Generic.List[string]]::new()
  $statePath=Get-DevStatePath $Root
  if(-not (Test-Path $statePath)){ $errors.Add("Developer state file missing: $statePath") }
  foreach($cmd in 'git','cmake','ninja','cargo','rustc'){if(-not (Test-Cmd $cmd)){$errors.Add("Missing tool: $cmd")}}
  $vcpkg=Join-Path $Root '.tooling\vcpkg\vcpkg.exe'; if(-not (Test-Path $vcpkg)){$errors.Add("Missing pinned vcpkg: $vcpkg")}
  if(-not (Test-Path (Join-Path $Root 'native\Cargo.lock'))){$errors.Add('native/Cargo.lock is missing')}
  if(Test-Cmd rustc){
    $rv=(& rustc --version 2>$null)
    if($rv -notmatch [regex]::Escape($script:RustVersion)){$errors.Add("Rust version mismatch. Expected $script:RustVersion, got $rv")}
  }
  if(Test-Path $statePath){
    try{$state=Get-Content $statePath -Raw|ConvertFrom-Json
      if($state.schema -ne $script:DevStateVersion){$errors.Add('Developer state schema mismatch')}
      if($state.rust -ne $script:RustVersion){$errors.Add('Developer state Rust version mismatch')}
      if($state.vcpkg_commit -ne (Get-GraphVisVcpkgBaseline $Root)){$errors.Add('Developer state vcpkg commit mismatch - the vcpkg.json baseline changed, run install.bat again')}
    }catch{$errors.Add('Developer state file is unreadable')}
  }
  [pscustomobject]@{Ok=($errors.Count -eq 0); Errors=@($errors); StatePath=$statePath}
}
function Write-GraphVisDevState([string]$Root){
  $tooling=Join-Path $Root '.tooling'; New-Item -ItemType Directory -Force -Path $tooling|Out-Null
  $state=[ordered]@{schema=$script:DevStateVersion;product='GraphVis';version='18.4.0';rust=$script:RustVersion;vcpkg_commit=(Get-GraphVisVcpkgBaseline $Root);completed_utc=[DateTime]::UtcNow.ToString('o')}
  $path=Get-DevStatePath $Root; $state|ConvertTo-Json|Set-Content $path -Encoding UTF8; $path
}
