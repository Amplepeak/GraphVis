# GraphVis 18.4 Migration Cleanup Log

Removed from the repository root because developer and user workflows must not overlap:

- `Start_GraphVis.bat`
- `Start_GraphVis.ps1`
- `Diagnose_GraphVis_Start.bat`
- `Developer_First_Time_Setup.bat`
- `Quick_Build_GraphVis_18.bat`
- `Build_GraphVis_18.bat`
- `Build_Full_Release_18.bat`

Removed installer residue:

- `installer/windows/GraphVis.iss`
- all Inno Setup / `ISCC.exe` release paths

Removed obsolete version-specific migration/setup documents:

- 18.3 acceleration/release/migration/verification notes
- obsolete Start Here source-package launcher instructions

Replacements:

- `installer/windows/GraphVis.nsi`
- `install.bat`
- `run.bat`
- `build.bat`
- `tools/Dev-State.ps1`
- `tools/Install-State.ps1`
- `packaging/internal/Runtime-Maintenance.ps1`
- separate Windows ZIP and Linux tar.gz public-release gates in CI
- root-level public install/run entry points with support files moved out of the way
- optional Python/Julia/R services excluded from the base runtime
- unused mandatory Graphviz bundle removed

The application executable itself no longer consults a developer bootstrap marker. End-user launch is direct through `graphvis.exe`.
