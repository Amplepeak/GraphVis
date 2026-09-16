<#
Kept as the older entry point. The component list, the install logic and the
"is it actually importable" test all live in Manage-OptionalComponents.ps1 now,
so this delegates rather than keeping a second copy that could disagree with it.

  -AllFormats   include the heavy domain formats
#>
param(
  [switch]$AllFormats
)
$ErrorActionPreference = 'Stop'
$manager = Join-Path $PSScriptRoot 'Manage-OptionalComponents.ps1'
if (-not (Test-Path $manager)) { throw "Manage-OptionalComponents.ps1 is missing from $PSScriptRoot" }

# The [vlm] component - transformers and torch - is never installed from here.
# It is not a dataset format, it is larger than everything else combined, and it
# is chosen deliberately from INSTALL-DATA-FORMATS.bat or from Add-ons.
$keys = if ($AllFormats) { 'io,marine,literature,reports,symbolic,units,uncertainty,io_extra' }
        else             { 'io,marine,literature,reports,symbolic,units,uncertainty' }

& $manager -Install $keys
exit $LASTEXITCODE
