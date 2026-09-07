# Check-Scripts - parse every PowerShell script in the repository.
#
# Two shipped scripts could never run. tools\Build-Release.ps1 had
#   $Build = Join-Path $Root (if($Fast){'a'}else{'b'})
# which Windows PowerShell 5.1 rejects outright - `if` is a statement and a
# statement cannot sit bare inside a parenthesised argument - and
# tools\Generate-RuntimeManifest.ps1 called .TrimStart('\\','/'), passing a
# two-character string where a char is required, which throws at runtime.
# Neither is exercised by the daily build, so the end-user release could not be
# produced and nobody found out for months.
#
# IMPORTANT: every .bat in this repository launches powershell.exe, which is
# Windows PowerShell 5.1, NOT pwsh 7. PowerShell 7 accepts syntax 5.1 rejects -
# statements as expressions, ternaries, ?? and ?. , && and || - so a script that
# is fine in pwsh can still be dead on arrival here. Run this the same way the
# .bat files run: powershell.exe -File tools\Check-Scripts.ps1
param([switch]$Quiet)
$ErrorActionPreference = 'Continue'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

$skip = '\\(build|\.tooling|\.cache|\.venv|dist|target|node_modules)\\'
$files = Get-ChildItem -Path $Root -Recurse -Filter *.ps1 -File -ErrorAction SilentlyContinue |
         Where-Object { $_.FullName -notmatch $skip } | Sort-Object FullName

# PowerShell 7 syntax that Windows PowerShell 5.1 cannot parse. Only reported
# when this check is itself running under 7 - under 5.1 they are parse errors
# already and are caught properly above.
$sevenOnly = @(
  @{ Pattern = '(?<![\w''"])\?\?='; Name = 'null-coalescing assignment ??=' },
  @{ Pattern = '(?<![\w''"])\?\?(?!=)'; Name = 'null-coalescing ??' },
  @{ Pattern = '\$\w+\?\.'; Name = 'null-conditional ?.' },
  @{ Pattern = '(?<![$\w])\((if|foreach|switch|while|do|try)[ (]'; Name = 'statement used as an expression in ( ) - needs $( )' },
  @{ Pattern = 'ForEach-Object\s+-Parallel'; Name = 'ForEach-Object -Parallel' }
)
$runningOn7 = $PSVersionTable.PSVersion.Major -ge 6

$failed = 0
$warned = 0
foreach ($f in $files) {
  $rel = $f.FullName.Substring($Root.Length).TrimStart([char]'\', [char]'/')
  $errors = $null
  $tokens = $null
  [void][System.Management.Automation.Language.Parser]::ParseFile($f.FullName, [ref]$tokens, [ref]$errors)
  if ($errors -and $errors.Count -gt 0) {
    $failed++
    Write-Host ("FAIL  {0}" -f $rel) -ForegroundColor Red
    foreach ($e in $errors) {
      Write-Host ("        line {0}: {1}" -f $e.Extent.StartLineNumber, $e.Message) -ForegroundColor Red
    }
    continue
  }
  $notes = @()
  # This file's own rule table contains every pattern it looks for, so scanning
  # it would report it as broken. It is parse-checked like everything else.
  if ($runningOn7 -and $f.Name -ne 'Check-Scripts.ps1') {
    $text = Get-Content -LiteralPath $f.FullName -Raw
    $lines = $text -split "`n"
    foreach ($rule in $sevenOnly) {
      for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if ($line -match '^\s*#') { continue }
        if ($line -match $rule.Pattern) {
          $notes += ("line {0}: {1}" -f ($i + 1), $rule.Name)
        }
      }
    }
  }
  if ($notes.Count -gt 0) {
    $warned++
    Write-Host ("WARN  {0}" -f $rel) -ForegroundColor Yellow
    foreach ($n in $notes) { Write-Host ("        {0} - rejected by Windows PowerShell 5.1" -f $n) -ForegroundColor Yellow }
  } elseif (-not $Quiet) {
    Write-Host ("ok    {0}" -f $rel)
  }
}

Write-Host ""
Write-Host ("Checked {0} script(s) with PowerShell {1}: {2} parse failure(s), {3} 5.1-compatibility warning(s)" -f `
  $files.Count, $PSVersionTable.PSVersion, $failed, $warned)
if ($failed -gt 0) { exit 1 }
exit 0
