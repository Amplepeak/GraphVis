"""Running a subprocess without a console window appearing.

Why this file exists
--------------------
Every `subprocess.run` on Windows opens a console window for the child, even
when the parent is a pythonw process that has none. The audit spawns `git`
five times and `check_definitions.py` once per pass, so a pass that happens
within seconds of every file save produced six black rectangles flashing over
whatever the person was doing. Correct output, unusable tool.

CREATE_NO_WINDOW is the fix, and the reason it lives in its own module is that
there is one rule here and it has to hold for every caller: the moment one
subprocess call is written without it, the flashing is back and it is not
obvious which call did it.
"""
from __future__ import annotations

import os
import subprocess

# 0x08000000. Named on Windows only, so it cannot simply be referenced.
CREATE_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0x08000000)


def _hidden_kwargs() -> dict:
    if os.name != "nt":
        return {}
    kw: dict = {"creationflags": CREATE_NO_WINDOW}
    # Belt and braces: some hosts ignore the creation flag, and STARTUPINFO
    # with SW_HIDE covers those. Both are cheap and neither is sufficient
    # everywhere on its own.
    try:
        si = subprocess.STARTUPINFO()
        si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        si.wShowWindow = subprocess.SW_HIDE
        kw["startupinfo"] = si
    except Exception:
        # no STARTUPINFO here; CREATE_NO_WINDOW above already hides the window
        pass
    return kw


def run(cmd: list, *, cwd=None, timeout: float = 60.0,
        text: bool = True) -> "subprocess.CompletedProcess | None":
    """Run `cmd`, invisibly, and never raise.

    Returns None when the command could not be run at all. A passive audit
    that dies because git is missing is worse than one that says nothing
    about the branch.
    """
    try:
        return subprocess.run(cmd, cwd=None if cwd is None else str(cwd),
                              capture_output=True, text=text, timeout=timeout,
                              **_hidden_kwargs())
    except (OSError, subprocess.SubprocessError):
        return None


def out(cmd: list, *, cwd=None, timeout: float = 60.0) -> str:
    r = run(cmd, cwd=cwd, timeout=timeout)
    return r.stdout.strip() if r is not None else ""
