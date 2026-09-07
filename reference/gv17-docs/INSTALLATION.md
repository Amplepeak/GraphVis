# GraphVis installation on Windows

## Launcher names

- `Install_GraphVis.bat` — first install or normal feature update (`recommended`).
- `Repair_GraphVis.bat` — repairs only the packages required to start GraphVis (`core --force`). Use this after a broken/partial install.
- `Start_GraphVis.bat` — normal friendly launcher.
- `run_app.bat` — launcher only; never installs packages.
- `update.bat` — actual installer/updater implementation.

## Important behavior

Core GUI/numerical packages are mandatory and installed from binary wheels only. Optional feature groups are also binary-wheel-only and are non-fatal: if Windows cannot obtain a compatible wheel for an optional package, GraphVis continues with the available features and records the skipped group in `Documents\GraphVis\Logs\pip-install.log`.

The recommended profile does not require the `netCDF4` C-extension. NetCDF ingestion uses `xarray` with `h5netcdf` or SciPy. `netCDF4` is attempted only as an optional full-profile backend.

## Logs

- `Documents\GraphVis\Logs\update.log` — updater stages.
- `Documents\GraphVis\Logs\pip-install.log` — pip details.
- `Documents\GraphVis\Logs\startup-preflight.log` — off-screen GUI construction check.
- `Documents\GraphVis\Logs\launcher.log` — Python startup traceback.
- `Documents\GraphVis\Logs\launcher-bat.log` — BAT launcher stages.

## Path-space hotfix

The requirements fingerprint uses a temporary file instead of fragile `FOR /F` command substitution, so paths such as `Documents\New graphs\...` are supported.
