"""Central filesystem layout for GraphVis.

The extracted application version is read-only/disposable. Mutable user data
and the Python runtime live outside the version folder so upgrades do not
reinstall dependencies or scatter projects/logs across copies of GraphVis.
"""
from __future__ import annotations

import os
import shutil
from pathlib import Path
from typing import Final

PACKAGE_DIR: Final[Path] = Path(__file__).resolve().parents[1]
INSTALL_ROOT: Final[Path] = PACKAGE_DIR.parents[1]
ASSETS_DIR: Final[Path] = INSTALL_ROOT / "assets" / "branding"
GRAPH_PREVIEW_DIR: Final[Path] = INSTALL_ROOT / "assets" / "graph_previews"
SHIPPED_CONFIG_DIR: Final[Path] = INSTALL_ROOT / "config" / "defaults"


def _documents() -> Path:
    override = os.environ.get("GRAPHVIS_HOME")
    if override:
        return Path(override).expanduser().resolve()
    # A visible location is intentional: projects/backups should be easy to find.
    home = Path.home()
    docs = home / "Documents"
    return (docs if docs.exists() or os.name == "nt" else home) / "GraphVis"


def _runtime() -> Path:
    override = os.environ.get("GRAPHVIS_RUNTIME")
    if override:
        return Path(override).expanduser().resolve()
    if os.name == "nt":
        base = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
        return base / "GraphVis" / "Runtime"
    xdg = os.environ.get("XDG_DATA_HOME")
    return (Path(xdg) if xdg else Path.home() / ".local" / "share") / "GraphVis" / "runtime"


USER_HOME: Final[Path] = _documents()
RUNTIME_HOME: Final[Path] = _runtime()
PROJECTS_DIR: Final[Path] = USER_HOME / "Projects"
IMPORTS_DIR: Final[Path] = USER_HOME / "Imports"
LITERATURE_DIR: Final[Path] = USER_HOME / "Literature"
# Numeric tables extracted from papers/theses land here as CSV datasets so the
# active project can reuse them as ordinary data sources.
LITERATURE_DATASET_DIR: Final[Path] = LITERATURE_DIR / "literature_dataset"
EXPORTS_DIR: Final[Path] = USER_HOME / "Exports"
TEMPLATES_DIR: Final[Path] = USER_HOME / "Templates"
LOG_DIR: Final[Path] = USER_HOME / "Logs"
ERROR_LOG_DIR: Final[Path] = LOG_DIR / "Errors"
DEBUG_LOG_DIR: Final[Path] = LOG_DIR / "Debug"
USER_CONFIG_DIR: Final[Path] = USER_HOME / "Config"
ARCHIVE_DIR: Final[Path] = USER_HOME / "Archive"
BACKUP_DIR: Final[Path] = USER_HOME / "Backups"
CACHE_DIR: Final[Path] = RUNTIME_HOME / "cache"
FIGURE_CACHE_DIR: Final[Path] = CACHE_DIR / "figures"
THUMB_CACHE_DIR: Final[Path] = CACHE_DIR / "thumbs"
ACTIVE_PROJECT_FILE: Final[Path] = USER_CONFIG_DIR / "active_project.json"
ALIAS_CONFIG_FILE: Final[Path] = USER_CONFIG_DIR / "variable_aliases.json"
LOG_FILE: Final[Path] = DEBUG_LOG_DIR / "graphvis.log"
ERROR_LOG_FILE: Final[Path] = ERROR_LOG_DIR / "errors.log"


def ensure_layout() -> None:
    for path in (USER_HOME, PROJECTS_DIR, IMPORTS_DIR, LITERATURE_DIR, LITERATURE_DATASET_DIR, EXPORTS_DIR,
                 TEMPLATES_DIR, LOG_DIR, ERROR_LOG_DIR, DEBUG_LOG_DIR, USER_CONFIG_DIR, ARCHIVE_DIR, BACKUP_DIR,
                 RUNTIME_HOME, CACHE_DIR, FIGURE_CACHE_DIR, THUMB_CACHE_DIR):
        path.mkdir(parents=True, exist_ok=True)
    if not ALIAS_CONFIG_FILE.exists():
        default_aliases = SHIPPED_CONFIG_DIR / "variable_aliases.json"
        if default_aliases.exists():
            shutil.copy2(default_aliases, ALIAS_CONFIG_FILE)
    marker = USER_HOME / "README - GraphVis Data Folders.txt"
    if not marker.exists():
        marker.write_text(
            "GraphVis user data is stored here so application upgrades are disposable.\n\n"
            "Projects  - project workspaces and project-local datasets\n"
            "Imports   - optional standalone source files\n"
            "Literature- extracted literature data\n"
            "Exports   - exported figures/reports\n"
            "Templates - reusable graph/analysis templates\n"
            "Logs      - application logs (Debug) and crash/error reports (Errors)\n"
            "Archive   - archived GraphVis versions/release packages\n"
            "Backups   - user-data backup archives\n\n"
            "The shared Python environment/cache lives under LocalAppData\\GraphVis\\Runtime on Windows.\n",
            encoding="utf-8",
        )


ensure_layout()
