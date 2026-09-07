# GraphVis installation and folder layout

## Two independent locations

### Application version
The extracted ZIP contains source, assets, documentation and launchers. It can be deleted/replaced without deleting projects or the Python environment.

### Shared GraphVis home/runtime
Windows defaults:

- User data: `%USERPROFILE%\Documents\GraphVis`
- Python runtime/cache: `%LOCALAPPDATA%\GraphVis\Runtime`

This solves the previous behavior where every new extraction created another multi-gigabyte `.venv`.

## Installer profiles

- `update.bat core` – minimum GUI/scientific runtime.
- `update.bat` or `update.bat recommended` – normal scientific analysis, formats and literature workflow.
- `update.bat full` – adds database/cloud, GPU/3D, GIS, Transformers and legacy instrument extras.
- `update.bat recommended --force` – force dependency verification/install even if the fingerprint is unchanged.
- `update.bat recommended --no-launch` – update only.

The requirements fingerprint is stored under the shared Runtime `stamps` directory. If a later GraphVis build contains the same requirement files, pip is skipped completely.

## Archiving

`tools\archive_version.bat` archives the current application version into `Documents\GraphVis\Archive\Versions`.

`tools\backup_user_data.bat` creates a separate backup of the important user-data folders. Runtime caches and the virtual environment are intentionally not included because they can be recreated.
