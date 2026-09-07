"""Print the distributions one pip extra installed, one per line.

Called by Manage-OptionalComponents.ps1 when removing a component. It lives in
its own file rather than a PowerShell here-string because a here-string
containing Python containing quotes and backslashes is a parsing accident
waiting to happen, and because this way it can be run and checked on its own.

    python component_requirements.py io_extra
"""
from __future__ import annotations

import re
import sys

DIST = "graphvis-science-service"


def requirements_for(extra: str) -> list[str]:
    from importlib.metadata import metadata

    marker = 'extra == "%s"' % extra
    alt = "extra == '%s'" % extra
    names: set[str] = set()
    for line in metadata(DIST).get_all("Requires-Dist") or []:
        text = str(line)
        if marker not in text and alt not in text:
            continue
        # "pyteomics>=4.7; extra == \"io_extra\"" -> "pyteomics"
        names.add(re.split(r"[<>=!;\[\s]", text.strip())[0])
    return sorted(n for n in names if n)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: component_requirements.py <extra>", file=sys.stderr)
        raise SystemExit(2)
    try:
        for name in requirements_for(sys.argv[1]):
            print(name)
    except Exception as exc:  # the package is not installed at all
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
