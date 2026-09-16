param([Parameter(Mandatory=$true)][string]$Stage)
$ErrorActionPreference='Stop'
$Stage=(Resolve-Path $Stage).Path
$manifest=Join-Path $Stage 'runtime-manifest.sha256'
Remove-Item $manifest -Force -ErrorAction SilentlyContinue
$lines = foreach($file in Get-ChildItem -LiteralPath $Stage -Recurse -File | Sort-Object FullName){
    if($file.FullName -eq $manifest){continue}
    # TrimStart takes chars: '\\' is a two-character string in PowerShell single
    # quotes and throws. Replace had the same fault - it was looking for a pair
    # of backslashes that never occurs in a path.
    $rel=$file.FullName.Substring($Stage.Length).TrimStart([char]'\',[char]'/').Replace('\','/')
    $hash=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $rel"
}
$lines | Set-Content -LiteralPath $manifest -Encoding ASCII
Write-Host "Runtime manifest: $manifest ($($lines.Count) files)" -ForegroundColor Green
