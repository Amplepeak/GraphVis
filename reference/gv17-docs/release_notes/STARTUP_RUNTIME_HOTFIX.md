# GraphVis startup runtime hotfix

## Fixed startup crash
`views/main_window.py` used `QSizePolicy` while constructing the Advanced UI but did not import it. This is a runtime `NameError`, so `compileall` did not catch it. The import is now present.

## Installer-owned preflight
Environment repair remains exclusively in `update.bat`. After dependency installation, `update.bat` now runs `startup_preflight.py` with Qt's off-screen platform. It constructs the MVC model/view/controller and processes initial layout events without launching the normal interactive application. Constructor/runtime failures are written to `logs/startup-preflight.log` and stop the updater before launch.

## Launch logging
`GraphVis.py` now delegates to `launch_graphvis.py`. If it is double-clicked and startup fails, the failure is written to `logs/launcher.log` and the Windows console waits for Enter instead of disappearing immediately.

Normal use remains:
1. Run `update.bat` after first extraction or after dependency changes.
2. Use `run_app.bat` for later launches.
