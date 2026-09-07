"""Persistent application logging for GraphVis.

Debug/runtime diagnostics and error/crash output are separated into visible
subfolders under Documents/GraphVis/Logs so a user can find the relevant file
without searching through installer output.
"""
from __future__ import annotations

import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path
from typing import Final

from graphvis.core.paths import ERROR_LOG_FILE, LOG_FILE, ensure_layout

ensure_layout()
APP_DIR: Final[Path] = Path(__file__).resolve().parent
LOGGER_NAME: Final[str] = "graphvis"


def _has_file_handler(logger: logging.Logger, path: Path) -> bool:
    target = path.resolve()
    for handler in logger.handlers:
        if isinstance(handler, RotatingFileHandler):
            try:
                if Path(getattr(handler, "baseFilename", "")).resolve() == target:
                    return True
            except Exception:
                pass
    return False


def configure_logging(level: int = logging.INFO) -> logging.Logger:
    logger = logging.getLogger(LOGGER_NAME)
    logger.setLevel(level)
    logger.propagate = False
    fmt = logging.Formatter("%(asctime)s | %(levelname)s | %(threadName)s | %(name)s | %(message)s")

    if not _has_file_handler(logger, LOG_FILE):
        debug_handler = RotatingFileHandler(LOG_FILE, maxBytes=6_000_000, backupCount=5, encoding="utf-8")
        debug_handler.setLevel(level)
        debug_handler.setFormatter(fmt)
        logger.addHandler(debug_handler)

    if not _has_file_handler(logger, ERROR_LOG_FILE):
        error_handler = RotatingFileHandler(ERROR_LOG_FILE, maxBytes=4_000_000, backupCount=8, encoding="utf-8")
        error_handler.setLevel(logging.ERROR)
        error_handler.setFormatter(fmt)
        logger.addHandler(error_handler)
    return logger


def get_logger(name: str | None = None) -> logging.Logger:
    configure_logging()
    return logging.getLogger(f"{LOGGER_NAME}.{name}" if name else LOGGER_NAME)


def log_exception(message: str, exc: BaseException) -> None:
    get_logger("exception").exception("%s: %s", message, exc)


configure_logging()
