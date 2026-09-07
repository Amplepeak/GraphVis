from __future__ import annotations
import logging, os
from pathlib import Path

def get_logger(name: str):
    return logging.getLogger(name)

LITERATURE_DIR = Path(os.environ.get("GRAPHVIS_LITERATURE_DIR", Path.home()/"Documents"/"GraphVis"/"18.4"/"literature_dataset"))


def literature_dir() -> Path:
    """The literature dataset folder, created on first use.

    This mkdir used to run at import time. Documents is routinely redirected to
    OneDrive, to a roaming profile or to a network share, so on a machine where
    that target is offline or read-only merely importing this module raised
    OSError - and because the literature extractor imports it, the whole
    science add-on reported itself broken rather than degrading to "the
    literature folder is unavailable".
    """
    LITERATURE_DIR.mkdir(parents=True, exist_ok=True)
    return LITERATURE_DIR
