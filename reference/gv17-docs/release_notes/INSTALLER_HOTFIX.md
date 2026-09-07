# GraphVis installer / startup hotfix

This build deliberately keeps dependency management out of `main_app.py`.

## Correct Windows startup sequence

1. Run `update.bat` once (and whenever dependencies need updating).
2. After the environment is ready, `update.bat` launches GraphVis automatically.
3. For later launches, use `run_app.bat`.
4. To launch `main_app.py` directly, use the GraphVis interpreter:
   `.venv\Scripts\python.exe main_app.py`

Running `python main_app.py` with an arbitrary system interpreter is not expected
to repair missing packages; that behaviour was intentionally removed.

## Why the NumPy/Meson error happened

The failing environment was attempting to build NumPy 1.26.4 from source and
could not find MSVC/GCC/Clang. GraphVis now standardises the Windows venv on
Python 3.12 and installs the core numerical/Qt stack from binary wheels only.
It never falls back to compiling the core stack.

If Python 3.12 is missing, `update.bat` first tries a per-user Python 3.12
installation through `winget`.

## Logs

- `logs\update.log`: updater stages, selected Python and setup status.
- `logs\pip-install.log`: detailed pip resolver/download/build log. This exists
  even if GraphVis never starts.
- `logs\launcher.log`: startup/import traceback before the GUI is available.
- `graphvis.log`: normal GraphVis application/MVC runtime log.
- `crash_debug_log.txt`: Qt/freeze-watchdog diagnostics.

`debug_run.py` now displays the tail of these logs before attempting third-party
imports, so it remains useful after an incomplete installation.
