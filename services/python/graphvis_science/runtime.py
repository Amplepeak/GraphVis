from __future__ import annotations
import logging, os
from pathlib import Path

def get_logger(name: str):
    return logging.getLogger(name)

LITERATURE_DIR = Path(os.environ.get("GRAPHVIS_LITERATURE_DIR", Path.home()/"Documents"/"GraphVis"/"18.4"/"literature_dataset"))


# THE FOLDER IS CREATED BY WHOEVER WRITES INTO IT.
#
# There was a `literature_dir()` here whose docstring explained that the mkdir
# used to run at import time, and that moving it out stopped a redirected or
# offline Documents folder from making the whole science add-on report itself
# broken. That reasoning is right and the fix is still in force - this module
# does no mkdir at all - but nothing ever called the function. The only place a
# literature dataset is written is `save_extraction`, which does its own
# `os.makedirs(target, exist_ok=True)`.
#
# It survived because an unused `from ... import literature_dir` in the
# extractor counted as a use. Removing that import is what made the dead-code
# check fire.
